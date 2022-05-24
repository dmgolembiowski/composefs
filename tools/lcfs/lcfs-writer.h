/* lcfs
   Copyright (C) 2021 Giuseppe Scrivano <giuseppe@scrivano.org>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _LCFS_OPS_H
#define _LCFS_OPS_H

#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "lcfs.h"

/* In memory representation used to build the file.  */

struct lcfs_xattr_s {
	char *key;
	char *value;
	size_t value_len;
};

struct lcfs_node_s {
	struct lcfs_node_s *next;

	struct lcfs_node_s *parent;

	struct lcfs_node_s **children;
	size_t children_size;

	/* Used to create hard links.  */
	struct lcfs_node_s *link_to;

	size_t index;

	bool inode_written;

	char *name;
	char *payload;

	struct lcfs_xattr_s *xattrs;
	size_t n_xattrs;

	struct lcfs_dentry_s data;

	struct lcfs_inode_s inode;
	struct lcfs_inode_data_s inode_data;

	struct lcfs_extend_s extend;
};

enum {
	BUILD_SKIP_XATTRS = (1 << 0),
	BUILD_USE_EPOCH = (1 << 1),
	BUILD_SKIP_DEVICES = (1 << 2),
};


struct lcfs_node_s *lcfs_node_new(void);
void lcfs_node_free(struct lcfs_node_s *node);
struct lcfs_node_s *lcfs_load_node_from_file(int dirfd,
					     const char *fname,
					     int flags,
					     int buildflags);
struct lcfs_node_s *lcfs_node_lookup_child(struct lcfs_node_s *node,
					   const char *name);
int lcfs_node_add_child(struct lcfs_node_s *parent,
			struct lcfs_node_s *child,
			const char *name);
int lcfs_node_append_xattr(struct lcfs_node_s *node,
			   const char *key,
			   const char *value, size_t value_len);
int lcfs_node_set_payload(struct lcfs_node_s *node,
			  const char *payload);


bool lcfs_node_dirp(struct lcfs_node_s *node);
uint32_t lcfs_node_get_mode(struct lcfs_node_s *node);
void lcfs_node_set_mode(struct lcfs_node_s *node,
			uint32_t mode);
uint32_t lcfs_node_get_uid(struct lcfs_node_s *node);
void lcfs_node_set_uid(struct lcfs_node_s *node,
		       uint32_t uid);
uint32_t lcfs_node_get_gid(struct lcfs_node_s *node);
void lcfs_node_set_gid(struct lcfs_node_s *node,
		       uint32_t gid);
uint32_t lcfs_node_get_rdev(struct lcfs_node_s *node);
void lcfs_node_set_rdev(struct lcfs_node_s *node,
			uint32_t rdev);
uint32_t lcfs_node_get_nlink(struct lcfs_node_s *node);
void lcfs_node_set_nlink(struct lcfs_node_s *node,
			 uint32_t nlink);
uint64_t lcfs_node_get_size(struct lcfs_node_s *node);
void lcfs_node_set_size(struct lcfs_node_s *node,
			uint64_t size);

struct lcfs_node_s *lcfs_build(struct lcfs_node_s *parent, int fd,
			       const char *fname, const char *name, int flags,
			       int buildflags);

int lcfs_write_to(struct lcfs_node_s *root, FILE *out);


#endif
