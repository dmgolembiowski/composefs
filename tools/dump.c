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

#define _GNU_SOURCE

#include "lcfs.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

static int get_file_size(int fd, off_t *out)
{
	struct stat sb;
	int ret;

	ret = fstat(fd, &sb);
	if (ret < 0)
		return ret;

	*out = sb.st_size;
	return 0;
}

static const uint8_t *get_vdata(const uint8_t *x)
{
	return x + sizeof(struct lcfs_header_s);
}

static uint64_t get_size(bool symlink_p,
			 const struct lcfs_inode_s *ino,
			 const uint8_t *vdata)
{
	struct lcfs_backing_s *backing;

	if (symlink_p)
		return 0;

	backing = (struct lcfs_backing_s *)(vdata + ino->u.backing.off);
	return backing->st_size;
}

static char *get_v_payload(bool symlink_p,
			   const struct lcfs_inode_s *ino,
			   const uint8_t *vdata)
{
	struct lcfs_backing_s *backing;

	if (symlink_p)
		return strdup((const char *)(vdata + ino->u.payload.off));

	if (ino->u.backing.len == 0)
		return strdup("");

	backing = (struct lcfs_backing_s *)(vdata + ino->u.backing.off);

	return strndup(backing->payload, backing->payload_len);
}

static bool is_dir(const struct lcfs_inode_s *d)
{
	return (d->st_mode & S_IFMT) == S_IFDIR;
}

static bool is_symlink(const struct lcfs_inode_s *d)
{
	return (d->st_mode & S_IFMT) == S_IFLNK;
}

static int dump_dentry(const uint8_t *vdata, const char *name, size_t name_len, size_t index,
		       size_t rec, bool extended, bool xattrs, bool recurse)
{
	struct lcfs_inode_s *ino;
	bool dirp;
	size_t i;

	ino = (struct lcfs_inode_s *)(vdata + index);
	dirp = is_dir(ino);

	putchar('|');
	for (i = 0; i < rec; i++)
		putchar('-');

	if (xattrs) {
		if (ino->xattrs.len != 0) {
			struct lcfs_xattr_header_s *header = (struct lcfs_xattr_header_s *)(vdata + ino->xattrs.off);
			uint8_t *data;

			data = ((uint8_t *)header) + lcfs_xattr_header_size(header->n_attr);
			for (i = 0; i < header->n_attr; i++) {
				uint32_t key_length = header->attr[i].key_length;
				uint32_t value_length = header->attr[i].value_length;

				printf("%.*s -> %.*s\n", (int)key_length, data,
				       (int)value_length, data + key_length);
				data += key_length + value_length;
			}
		}
	} else if (!extended)
		printf("%.*s\n", (int)name_len, name);
	else {
		char *payload = dirp ? strdup("") : get_v_payload(is_symlink(ino), ino, vdata);
		printf("name:%.*s|ino:%zu|mode:%o|nlinks:%u|uid:%d|gid:%d|size:%lu|payload:%s\n",
		       (int)name_len, name, index, ino->st_mode, ino->st_nlink,
		       ino->st_uid, ino->st_gid,
		       dirp ? 0 : get_size(is_symlink(ino), ino, vdata),
		       payload);
		free(payload);
	}

	if (dirp && recurse && ino->u.dir.len != 0) {
		const char *namedata;
		const struct lcfs_dir_s *dir;
		size_t n_dentries, child_name_len;

		dir = (const struct lcfs_dir_s *)(vdata + ino->u.dir.off);
		n_dentries = dir->n_dentries;

		namedata = (char *)dir + lcfs_dir_size(n_dentries);
		for (i = 0; i < n_dentries; i++) {
			child_name_len = dir->dentries[i].name_len;
			dump_dentry(vdata, namedata, child_name_len,
                                    dir->dentries[i].inode_index,
				    rec + 1, extended, xattrs, recurse);
			namedata += child_name_len;
		}
	}

	return 0;
}

static size_t find_child(const uint8_t *vdata, size_t current, const char *name)
{
	const struct lcfs_inode_s *ino = (const struct lcfs_inode_s *)(vdata + current);
	const char *namedata;
	const struct lcfs_dir_s *dir;
	size_t i, n_dentries, name_len;

	if (!is_dir(ino))
		return SIZE_MAX;

	if (ino->u.dir.len == 0)
		return SIZE_MAX;

	dir = (const struct lcfs_dir_s *)(vdata + ino->u.dir.off);
	n_dentries = dir->n_dentries;

	name_len = strlen(name);
	namedata = (char *)dir + lcfs_dir_size(n_dentries);
	for (i = 0; i < n_dentries; i++) {
		if (name_len == dir->dentries[i].name_len &&
		    memcmp(name, namedata, name_len) == 0) {
			return dir->dentries[i].inode_index;
		}
		namedata += dir->dentries[i].name_len;
	}

	return SIZE_MAX;
}

static const struct lcfs_inode_s *lookup(const uint8_t *vdata, size_t current,
					  const void *what)
{
	char *it;
	char *dpath, *path;

	if (strcmp(what, "/") == 0)
		return (struct lcfs_inode_s *)(vdata + current);

	path = strdup(what);
	if (path == NULL)
		error(EXIT_FAILURE, errno, "strdup");

	dpath = path;
	while ((it = strsep(&dpath, "/"))) {
		if (strlen(it) == 0)
			continue; /* Skip initial, terminal or repeated slashes */
		current = find_child(vdata, current, it);
		if (current == SIZE_MAX) {
			errno = ENOENT;
			free(path);
			return NULL;
		}
	}

	free(path);
	return (struct lcfs_inode_s *)(vdata + current);
}

#define DUMP 1
#define LOOKUP 2
#define XATTRS 3
#define DUMP_EXTENDED 4

int main(int argc, char *argv[])
{
	uint8_t *data;
	off_t size;
	int ret;
	int fd;
	int mode;
	size_t root_index;

	if (argc < 3)
		error(EXIT_FAILURE, errno, "argument not specified");

	if (strcmp(argv[1], "dump") == 0) {
		mode = DUMP;
	} else if (strcmp(argv[1], "lookup") == 0) {
		if (argc < 4)
			error(EXIT_FAILURE, errno, "argument not specified");
		mode = LOOKUP;
	} else if (strcmp(argv[1], "xattrs") == 0) {
		if (argc < 4)
			error(EXIT_FAILURE, errno, "argument not specified");
		mode = XATTRS;
	} else if (strcmp(argv[1], "dump-extended") == 0) {
		mode = DUMP_EXTENDED;
	} else {
		error(EXIT_FAILURE, 0, "invalid mode");
	}

	fd = open(argv[2], O_RDONLY);
	if (fd < 0)
		error(EXIT_FAILURE, errno, "open %s", argv[1]);

	ret = get_file_size(fd, &size);
	if (ret < 0)
		error(EXIT_FAILURE, errno, "read file size %s", argv[1]);

	data = (uint8_t *)mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (data == NULL)
		error(EXIT_FAILURE, errno, "fstat %s", argv[1]);

	root_index = size - sizeof(struct lcfs_header_s) -
		     sizeof(struct lcfs_inode_s);
	if (mode == DUMP) {
		dump_dentry(get_vdata(data), "", 0, root_index, 0, false, false, true);
	} else if (mode == DUMP_EXTENDED) {
		dump_dentry(get_vdata(data), "", 0, root_index, 0, true, false, true);
	} else if (mode == LOOKUP) {
		const struct lcfs_inode_s *inode;
		size_t index;

		inode = lookup(get_vdata(data), root_index, argv[3]);
		if (inode == NULL)
			error(EXIT_FAILURE, 0, "file %s not found", argv[3]);

		index = (uint8_t *)inode - get_vdata(data);
		dump_dentry(get_vdata(data), "", 0, index, 0, true, false, false);
	} else if (mode == XATTRS) {
		const struct lcfs_inode_s *inode;
		size_t index;

		inode = lookup(get_vdata(data), root_index, argv[3]);
		if (inode == NULL)
			error(EXIT_FAILURE, 0, "file %s not found", argv[3]);

		index = (uint8_t *)inode - get_vdata(data);
		dump_dentry(get_vdata(data), "", 0, index, 0, true, true, false);
	}

	munmap(data, size);
}
