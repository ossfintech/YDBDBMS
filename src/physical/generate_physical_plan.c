/* Copyright (C) 2018 YottaDB, LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "octo.h"
#include "octo_types.h"
#include "physical_plan.h"

#include "template_helpers.h"

void gen_source_keys(PhysicalPlan *out, LogicalPlan *plan);
void iterate_keys(PhysicalPlan *out, LogicalPlan *plan);
LogicalPlan *walk_where_statement(PhysicalPlan *out, LogicalPlan *stmt);

PhysicalPlan *generate_physical_plan(LogicalPlan *plan, PhysicalPlan *next) {
	SqlOptionalKeyword *keywords, *keyword;
	LogicalPlan *keys, *table_joins, *select, *insert, *output_key, *output;
	PhysicalPlan *out, *prev = NULL;
	char buffer[MAX_STR_CONST], *temp;
	SqlTable *table;
	SqlValue *value;
	SqlStatement *stmt;
	char *table_name;
	int len, table_count, i;

	// If this is a union plan, construct physical plans for the two children
	if(plan->type == LP_SET_OPERATION) {
		out = generate_physical_plan(plan->v.operand[1]->v.operand[1], next);
		prev = generate_physical_plan(plan->v.operand[1]->v.operand[0], out);

		// Switch what operation the second plan does
		switch(plan->v.operand[0]->v.operand[0]->type) {
		case LP_SET_UNION:
		case LP_SET_UNION_ALL:
			out->set_operation = PP_UNION_SET;
			out->action_type = PP_PROJECT;
			break;
		case LP_SET_EXCEPT:
		case LP_SET_EXCEPT_ALL:
			out->set_operation = PP_EXCEPT_SET;
			out->action_type = PP_DELETE;
			break;
		case LP_SET_INTERSECT:
		case LP_SET_INTERSECT_ALL:
			out->set_operation = PP_INTERSECT_SET;
			out->action_type = PP_DELETE;
			break;
		}

		// If the SET is not an "ALL" type, we need to keep resulting rows
		//  unique
		switch(plan->v.operand[0]->v.operand[0]->type) {
		case LP_SET_UNION:
		case LP_SET_EXCEPT:
		case LP_SET_INTERSECT:
			out->distinct_values = TRUE;
			prev->distinct_values = TRUE;
			out->maintain_columnwise_index = TRUE;
			prev->maintain_columnwise_index = TRUE;
			break;
		case LP_SET_UNION_ALL:
		case LP_SET_EXCEPT_ALL:
		case LP_SET_INTERSECT_ALL:
			out->maintain_columnwise_index = TRUE;
			prev->maintain_columnwise_index = TRUE;
			break;
		}
		return prev;
	}

	// Make sure the plan is in good shape
	if(lp_verify_structure(plan) == FALSE) {
		/// TODO: replace this with a real error message
		FATAL(CUSTOM_ERROR, "Bad plan!");
	}
	out = (PhysicalPlan*)malloc(sizeof(PhysicalPlan));
	memset(out, 0, sizeof(PhysicalPlan));
	out->next = next;

	// Set my output key
	GET_LP(output, plan, 1, LP_OUTPUT);
	if(output->v.operand[0]->type == LP_KEY) {
		GET_LP(output_key, output, 0, LP_KEY);
		out->outputKey = output_key->v.key;
	} else if(output->v.operand[0]->type == LP_TABLE) {
		out->outputKey = NULL;
		out->outputTable = output->v.operand[1]->v.table_alias;
	} else {
		assert(FALSE);
	}

	// If there is an order by, note it down
	if(output->v.operand[1]) {
		GET_LP(out->order_by, output, 1, LP_COLUMN_LIST);
	}
	// If there is someone next, my output key should be their first
	//  input key
	/// TODO: we should set next->sourceKeys[total_keys++] = outputKey
	///  where outputKey is generated by looking at the projection?
	/// TODO: we need to set the random_id here
	if(out->next) {
		// Add this key to the list of keys from that plan?
		out->next->sourceKeys[out->next->total_source_keys++] =
			out->outputKey;

	}

	// See if there are any tables we rely on in the SELECT or WHERE;
	//  if so, add them as prev records
	select = lp_get_select(plan);
	GET_LP(table_joins, select, 0, LP_TABLE_JOIN);
	do {
		// If this is a plan that doesn't have a source table,
		//  this will be null and we need to skip this step
		if(table_joins->v.operand[0] == NULL)
			break;
		table_count++;
		if(table_joins->v.operand[0]->type == LP_INSERT) {
			// This is a sub plan, and should be inserted as prev
			GET_LP(insert, table_joins, 0, LP_INSERT);
			if(prev == NULL) {
				prev = generate_physical_plan(insert, out);
				out->prev = prev;
			} else {
				prev->prev = generate_physical_plan(insert, prev);
				prev = prev->prev;
			}
		}
		if(table_joins->v.operand[1] != NULL)
				table_joins = table_joins->v.operand[1];
	} while(table_joins->v.operand[1] != NULL);

	// Iterate through the key substructures and fill out the source keys
	keys = lp_get_keys(plan);
	// All tables should have at least one key
	assert(keys != NULL);
	// Either we have some keys already, or we have a list of keys
	assert(out->total_iter_keys > 0 || keys->v.operand[0] != NULL);
	if(keys->v.operand[0])
		iterate_keys(out, keys);

	// The output key should be a cursor key

	// Is this most convenient representation of the WHERE?
	out->where = walk_where_statement(out, lp_get_select_where(plan));
	out->keywords = lp_get_select_keywords(plan)->v.keywords;

	out->projection = lp_get_projection_columns(plan);
	// As a temporary measure, wrap all tables in a SqlTableAlias
	//  so that we can search through them later;
	out->total_symbols = table_count;
	if(table_count > 0)
		out->symbols = (SqlTableAlias**)malloc(table_count * sizeof(SqlTableAlias*));
	GET_LP(table_joins, select, 0, LP_TABLE_JOIN);

	// Check the optional words for distinct
	keywords = lp_get_select_keywords(plan)->v.keywords;
	keyword = get_keyword_from_keywords(keywords, OPTIONAL_DISTINCT);
	if(keyword != NULL) {
		out->distinct_values = 1;
		out->maintain_columnwise_index = 1;
	}

	if(prev != NULL)
		out = prev;
	return out;
}

void iterate_keys(PhysicalPlan *out, LogicalPlan *plan) {
	LogicalPlan *left, *right;
	assert(plan->type == LP_KEYS);

	GET_LP(left, plan, 0, LP_KEY);
	out->iterKeys[out->total_iter_keys] = left->v.key;
	out->total_iter_keys++;

	if(plan->v.operand[1] != NULL) {
		GET_LP(right, plan, 1, LP_KEYS);
		iterate_keys(out, right);
	}
}

LogicalPlan *walk_where_statement(PhysicalPlan *out, LogicalPlan *stmt) {
	LogicalPlan *ret = NULL;
	LPActionType type;
	SqlValue *value;
	SqlBinaryOperation *binary;
	PhysicalPlan *t;
	LogicalPlan *project, *column_list, *where, *column_alias;

	if(stmt == NULL)
		return NULL;

	if(stmt->type >= LP_ADDITION && stmt->type <= LP_BOOLEAN_IN) {
		// This is a binary operation; clone it, and reasign the left-right options
		MALLOC_LP(ret, stmt->type);
		ret->v.operand[0] = walk_where_statement(out, stmt->v.operand[0]);
		ret->v.operand[1] = walk_where_statement(out, stmt->v.operand[1]);
	} else {
		switch(stmt->type) {
		case LP_DERIVED_COLUMN:
			ret = stmt;
			break;
		case LP_WHERE:
			MALLOC_LP(ret, stmt->type);
			ret->v.operand[0] = walk_where_statement(out, stmt->v.operand[0]);
			break;
		case LP_COLUMN_ALIAS:
			MALLOC_LP(ret, stmt->type);
			ret->v.column_alias = stmt->v.column_alias;
			break;
		case LP_VALUE:
			MALLOC_LP(ret, stmt->type);
			ret->v.value = stmt->v.value;
			break;
		case LP_INSERT:
			// Insert this to the physical plan, then create a
			//  reference to the first item in the column list
			t = out;
			while(t->prev != NULL)
				t = t->prev;
			t->prev = generate_physical_plan(stmt, t);
			/*GET_LP(project, stmt, 0, LP_PROJECT);
			GET_LP(column_list, project, 0, LP_COLUMN_LIST);
			GET_LP(where, column_list, 0, LP_WHERE);
			GET_LP(column_alias, where, 0, LP_COLUMN_ALIAS);*/
			t->prev->stash_columns_in_keys = 1;
			MALLOC_LP(ret, LP_KEY);
			ret->v.key = t->prev->outputKey;
			break;
		case LP_TABLE:
			// This should never happen; fall through to error case
		default:
			FATAL(ERR_UNKNOWN_KEYWORD_STATE);
			break;
		}
	}
	return ret;
}
