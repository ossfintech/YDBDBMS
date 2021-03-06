/* Copyright (C) 2018-2019 YottaDB, LLC
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
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

// Used to convert between network and host endian
#include <arpa/inet.h>

#include "octod.h"
#include "message_formats.h"

int read_bytes(OctodSession *session, char *buffer, int buffer_size, int bytes_to_read) {
	int read_so_far = 0, read_now = 0;

	while(read_so_far < bytes_to_read) {
		read_now = read(session->connection_fd, &buffer[read_so_far],
				bytes_to_read - read_so_far);
		if(read_now < 0) {
			if(errno == EINTR)
				continue;
			if(errno == ECONNRESET)
				return 1;
			FATAL(ERR_SYSCALL, "read", errno);
			return 1;
		}
		read_so_far += read_now;
	};

	return 0;
}
