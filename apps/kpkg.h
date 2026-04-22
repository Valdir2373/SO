#ifndef _KPKG_H
#define _KPKG_H

#include <types.h>


#define KPKG_MAGIC      "KPKG"
#define KPKG_VERSION    1
#define KPKG_PATH_MAX   256
#define KPKG_DB_DIR     "/var/db/kpkg"
#define KPKG_PKG_DIR    "/packages"
#define KPKG_REPOS_FILE "/etc/kpkg/repos"
#define KPKG_INDEX_FILE "/var/db/kpkg/index"
#define KPKG_DL_CHUNK   4096

typedef void (*kpkg_print_fn)(const char *s);


int kpkg_install(const char *pkgpath, kpkg_print_fn print);

void kpkg_list(kpkg_print_fn print);

void kpkg_search(const char *name, kpkg_print_fn print);

int kpkg_parse_url(const char *url, char *host_out, uint16_t *port_out, char *path_out);

#endif
