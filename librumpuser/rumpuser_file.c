#include "rumpuser_port.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <rump/rumpuser.h>

#include "rumpuser_int.h"

#include "fdinit.h"

int
rumpuser_getfileinfo(const char *path, uint64_t *sizep, int *ftp)
{
	int fd = atoi(path);
	int ft;

	if (fd < 0 || fd >= MAXFD || __franken_fd[fd].valid == 0)
		return ENOENT;

	if (sizep)
		*sizep = __franken_fd[fd].st.st_size;

	switch (__franken_fd[fd].st.st_mode & S_IFMT) {
	case S_IFDIR:
		ft = RUMPUSER_FT_DIR;
		break;
	case S_IFREG:
		ft = RUMPUSER_FT_REG;
		break;
	case S_IFBLK:
		ft = RUMPUSER_FT_BLK;
		break;
	case S_IFCHR:
		ft = RUMPUSER_FT_CHR;
		break;
	default:
		ft = RUMPUSER_FT_OTHER;
		break;
	}

	if (ftp)
		*ftp = ft;

	return 0;
}

int
rumpuser_open(const char *path, int ruflags, int *fdp)
{
	int fd = atoi(path);

	if (fd < 0 || fd >= MAXFD || __franken_fd[fd].valid == 0)
		return ENOENT;

	*fdp = fd;

	return 0;
}

int
rumpuser_close(int fd)
{

	return 0;
}

int
rumpuser_iovread(int fd, struct rumpuser_iovec *ruiov, size_t iovlen,
	int64_t roff, size_t *retp)
{
	size_t i;
	off_t size = __franken_fd[fd].st.st_size;
	size_t n = 0;

	/* Only used eg in device drivers, ignore */
	if (roff == RUMPUSER_IOV_NOSEEK)
		roff = 0;

	if (roff > size) {
		*retp = 0;
		return 0;
	}

	for (i = 0; i < iovlen; i++) {
		size_t len = ruiov[i].iov_len;

		if (len > size - roff)
			len = size - roff;
		memcpy(ruiov[i].iov_base, __franken_fd[fd].mem + roff, len);
		n += len;
		roff += len;
		if (len != ruiov[i].iov_len)
			break;
	}

	*retp = n;
	return 0;
}

int
rumpuser_iovwrite(int fd, const struct rumpuser_iovec *ruiov, size_t iovlen,
	int64_t roff, size_t *retp)
{
	size_t i;
	off_t size = __franken_fd[fd].st.st_size;
	size_t n = 0;

	/* Only used eg in device drivers, ignore */
	if (roff == RUMPUSER_IOV_NOSEEK)
		roff = 0;

	if (roff > size) {
		*retp = 0;
		return 0;
	}

	for (i = 0; i < iovlen; i++) {
		size_t len = ruiov[i].iov_len;

		if (len > size - roff)
			len = size - roff;
		memcpy(__franken_fd[fd].mem + roff, ruiov[i].iov_base, len);
		n += len;
		roff += len;
		if (len != ruiov[i].iov_len)
			break;
	}

	*retp = n;
	return 0;
}

int
rumpuser_syncfd(int fd, int flags, uint64_t start, uint64_t len)
{

	/* XXX would need msync, but not used anywhere in tree */
	return 0;
}

/* XXX probably do not want to mmap block devices, use pread, pwrite */
void
rumpuser_bio(int fd, int op, void *data, size_t dlen, int64_t doff,
        rump_biodone_fn biodone, void *bioarg)
{
	off_t size = __franken_fd[fd].st.st_size;

	assert(biodone != NULL);

	if (__franken_fd[fd].valid == 0 || ! S_ISREG(__franken_fd[fd].st.st_mode))
		biodone(bioarg, 0, EBADF);

	if (doff > size)
		dlen = 0;

	if (dlen > size - doff)
		dlen = size - doff;

	if (op & RUMPUSER_BIO_READ)
		memcpy(data, __franken_fd[fd].mem + doff, dlen);
	else
		memcpy(__franken_fd[fd].mem + doff, data, dlen);

	/* XXX support sync */

	biodone(bioarg, (size_t)dlen, 0);
}
