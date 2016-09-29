#include <ftw.h>

#if defined(FTW_ACTIONRETVAL) && defined(FTW_CONTINUE) && defined(FTW_SKIP_SUBTREE)
#define au_nftw nftw
#else

#ifndef FTW_CONTINUE
#define FTW_CONTINUE 0
#endif

#ifndef FTW_SKIP_SUBTREE
#define FTW_SKIP_SUBTREE 2
#endif

#ifndef FTW_ACTIONRETVAL
#define FTW_ACTIONRETVAL 16
#endif

int au_nftw(const char *path, int (*fn)(const char *, const struct stat *, int, struct FTW *), int fd_limit, int flags);
#endif
