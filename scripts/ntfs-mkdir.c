/*
 * FUSE-free NTFS directory creator for SentinelOS image staging.
 * Uses libntfs-3g directly so this works in containers/proot where mounting
 * a filesystem is not permitted.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <ntfs-3g/dir.h>
#include <ntfs-3g/volume.h>
#include <ntfs-3g/unistr.h>

static int make_directory(ntfs_volume *vol, const char *path)
{
    char *copy = strdup(path);
    char *slash;
    char *leaf;
    char *parent_path;
    ntfs_inode *parent = NULL;
    ntfs_inode *created = NULL;
    ntfschar *unicode = NULL;
    int name_len;
    int result = -1;

    if (!copy || path[0] != '/' || path[1] == '\0') {
        fprintf(stderr, "ntfs-mkdir: invalid absolute path: %s\n", path);
        free(copy);
        return -1;
    }

    slash = strrchr(copy, '/');
    leaf = slash + 1;
    if (!*leaf) {
        fprintf(stderr, "ntfs-mkdir: invalid directory path: %s\n", path);
        goto out;
    }
    if (slash == copy) {
        parent_path = "/";
    } else {
        *slash = '\0';
        parent_path = copy;
    }

    parent = ntfs_pathname_to_inode(vol, NULL, parent_path);
    if (!parent) {
        fprintf(stderr, "ntfs-mkdir: cannot open parent of %s: %s\n",
                path, strerror(errno));
        goto out;
    }

    name_len = ntfs_mbstoucs(leaf, &unicode);
    if (name_len <= 0 || !unicode) {
        fprintf(stderr, "ntfs-mkdir: cannot convert directory name: %s\n", leaf);
        goto out;
    }

    created = ntfs_create(parent, 0, unicode, (u8)name_len, S_IFDIR);
    if (!created) {
        fprintf(stderr, "ntfs-mkdir: cannot create %s: %s\n",
                path, strerror(errno));
        goto out;
    }

    if (ntfs_inode_close(created)) {
        created = NULL;
        fprintf(stderr, "ntfs-mkdir: cannot close new directory %s: %s\n",
                path, strerror(errno));
        goto out;
    }
    created = NULL;
    result = 0;

out:
    if (created)
        ntfs_inode_close(created);
    if (parent)
        ntfs_inode_close(parent);
    free(unicode);
    free(copy);
    return result;
}

int main(int argc, char **argv)
{
    ntfs_volume *vol;
    int i;
    int result = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: %s IMAGE /absolute/dir [...]\n", argv[0]);
        return 2;
    }

    if (ntfs_set_char_encoding("UTF-8")) {
        fprintf(stderr, "ntfs-mkdir: cannot set UTF-8 filename encoding\n");
        return 1;
    }

    vol = ntfs_mount(argv[1], NTFS_MNT_NONE);
    if (!vol) {
        fprintf(stderr, "ntfs-mkdir: cannot open NTFS image %s: %s\n",
                argv[1], strerror(errno));
        return 1;
    }

    for (i = 2; i < argc; ++i) {
        if (make_directory(vol, argv[i])) {
            result = 1;
            break;
        }
    }

    if (ntfs_umount(vol, FALSE)) {
        fprintf(stderr, "ntfs-mkdir: warning: NTFS unmount reported: %s\n",
                strerror(errno));
        result = 1;
    }
    return result;
}
