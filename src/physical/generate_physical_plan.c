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
	LogicalPlan *keys, *table_joins, *select, *insert, *output_key;
	PhysicalPlan *out, *prev = NULL;
	char buffer[MAX_STR_CONST], *temp;
	SqlTable *table;
	SqlValue *value;
	SqlStatement *stmt;
	char *table_name;
	int len, table_count, i;
	// Make sure the plan is in good shape
	if(lp_verify_structure(plan) == FALSE) {
		/// TODO: replace this with a real error message
		FATAL(CUSTOM_ERROR, "Bad plan!");
	}
	out = (PhysicalPlan*)malloc(sizeof(PhysicalPlan));
	memset(out, 0, sizeof(PhysicalPlan));
	out->next = next;

	// Set my output key
	if(plan->v.operand[1]->type == LP_KEY) {
		GET_LP(output_key, plan, 1, LP_KEY);
		out->outputKey = output_key->v.key;
	} else if(plan->v.operand[1]->type == LP_TABLE) {
		out->outputKey = NULL;
		out->outputTable = plan->v.operand[1]->v.table_alias;
	} else {
		assert(FALSE);
	}
	// If there is someone next, my output key should be their first
	//  input key
	/// TODO: we should set next->sourceKeys[total_keys++] = outputKey
	///  where outputKey is generated by looking at the projection?
	/*out->outputKey = (SqlKey*)malloc(sizeof(SqlKey));
	memset(out->outputKey, 0, sizeof(SqlKey));
	// If the table/column is NULL, the key will be assigned
	//  a value like tempTbl<unique ID> in the ^cursor
	//  Although, that may not be needed since each key has a unique
	//  value as part of it
	// Set the advance override to be
	//  $O(^cursor(cursorId,"keys",1337," "," ",^cursor(cursorId,"keys",1337," "," "))
	// In other words, the root node contains the last value, and we iterate
	//  through the leafe notes which contain all the values
	out->outputKey->type = LP_KEY_ADVANCE;
	out->outputKey->random_id = get_plan_unique_number(plan);
	len = tmpl_temp_key_advance(buffer, MAX_STR_CONST, out->outputKey);
	temp = malloc(len);
	memcpy(temp, buffer, len);
	out->outputKey->value = (SqlValue*)malloc(sizeof(SqlValue));
	out->outputKey->value->type = STRING_LITERAL;
	out->outputKey->value->v.string_literal = temp;*/
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
		table_count++;
		if(table_joins->v.operand[0]->type == LP_INSERT) {
			// This is a sub plan, and should be inserted as prev
			GET_LP(insert, table_joins, 0, LP_INSERT);
			if(prev == NULL) {
				prev = generate_physical_plan(insert, prev);
				out->prev = prev;
			} else {
				prev->prev = generate_physical_plan(insert, prev);
				prev = prev->prev;
			}
		} else {
			if(table_joins->v.operand[1] != NULL)
				table_joins = table_joins->v.operand[1];
		}
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

	out->projection = lp_get_projection_columns(plan);
	// As a temporary measure, wrap all tables in a SqlTableAlias
	//  so that we can search through them later;
	out->total_symbols = table_count;
	if(table_count > 0)
		out->symbols = (SqlTableAlias**)malloc(table_count * sizeof(SqlTableAlias*));
	GET_LP(table_joins, select, 0, LP_TABLE_JOIN);
	/*i = 0;
	do {
		assert(table_joins->v.operand[0]->type == LP_TABLE);
		out->symbols[i] = table_joins->v.operand[0]->v.table_alias;
		i++;
		} while(table_joins->v.operand[1] != NULL);*/
	//assert(i == table_count);

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