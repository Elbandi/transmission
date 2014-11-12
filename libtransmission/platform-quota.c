/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> /* getuid() */
#include <event2/util.h> /* evutil_ascii_strcasecmp () */

#ifndef _WIN32
 #include <sys/types.h> /* types needed by quota.h */
 #if defined(__FreeBSD__) || defined(__OpenBSD__)
  #include <ufs/ufs/quota.h> /* quotactl() */
 #elif defined (__NetBSD__)
  #include <sys/param.h>
  #ifndef statfs
   #define statfs statvfs
  #endif
 #elif defined (__sun)
  #include <sys/fs/ufs_quota.h> /* quotactl */
 #else
  #include <sys/quota.h> /* quotactl() */
 #endif
 #ifdef HAVE_GETMNTENT
  #ifdef __sun
   #include <sys/types.h>
   #include <sys/stat.h>
   #include <fcntl.h>
   #include <stdio.h>
   #include <sys/mntent.h>
   #include <sys/mnttab.h>
   #define _PATH_MOUNTED MNTTAB
  #else
   #include <mntent.h>
   #include <paths.h> /* _PATH_MOUNTED */
  #endif
 #else /* BSD derived systems */
  #include <sys/param.h>
  #include <sys/ucred.h>
  #include <sys/mount.h>
 #endif
#endif

#ifdef __APPLE__
 #define HAVE_SYS_STATVFS_H
 #define HAVE_STATVFS
#endif

#ifdef HAVE_SYS_STATVFS_H
 #include <sys/statvfs.h>
#endif

#ifdef HAVE_XFS_XFS_H
 #define HAVE_XQM
 #include <xfs/xqm.h>
#endif

#include "transmission.h"
#include "utils.h"
#include "platform-quota.h"

/***
****
***/

#ifndef _WIN32
static const char *
getdev (const char * path)
{
#ifdef HAVE_GETMNTENT

  FILE * fp;

#ifdef __sun
  struct mnttab mnt;
  fp = fopen(_PATH_MOUNTED, "r");
  if (fp == NULL)
    return NULL;

  while (getmntent(fp, &mnt))
    if (!tr_strcmp0 (path, mnt.mnt_mountp))
      break;
  fclose(fp);
  return mnt.mnt_special;
#else
  struct mntent * mnt;

  fp = setmntent(_PATH_MOUNTED, "r");
  if (fp == NULL)
    return NULL;

  while ((mnt = getmntent(fp)) != NULL)
    if (!tr_strcmp0 (path, mnt->mnt_dir))
      break;

  endmntent(fp);
  return mnt ? mnt->mnt_fsname : NULL;
#endif
#else /* BSD derived systems */

  int i;
  int n;
  struct statfs * mnt;

  n = getmntinfo(&mnt, MNT_WAIT);
  if (!n)
    return NULL;

  for (i=0; i<n; i++)
    if (!tr_strcmp0 (path, mnt[i].f_mntonname))
      break;

  return (i < n) ? mnt[i].f_mntfromname : NULL;

#endif
}

static const char *
getfstype (const char * device)
{

#ifdef HAVE_GETMNTENT

  FILE * fp;
#ifdef __sun
  struct mnttab mnt;
  fp = fopen(_PATH_MOUNTED, "r");
  if (fp == NULL)
    return NULL;
  while (getmntent(fp, &mnt))
    if (!tr_strcmp0 (device, mnt.mnt_mountp))
      break;
  fclose(fp);
  return mnt.mnt_fstype;
#else
  struct mntent *mnt;

  fp = setmntent (_PATH_MOUNTED, "r");
  if (fp == NULL)
    return NULL;

  while ((mnt = getmntent (fp)) != NULL)
    if (!tr_strcmp0 (device, mnt->mnt_fsname))
      break;

  endmntent(fp);
  return mnt ? mnt->mnt_type : NULL;
#endif
#else /* BSD derived systems */

  int i;
  int n;
  struct statfs *mnt;

  n = getmntinfo(&mnt, MNT_WAIT);
  if (!n)
    return NULL;

  for (i=0; i<n; i++)
    if (!tr_strcmp0 (device, mnt[i].f_mntfromname))
      break;

  return (i < n) ? mnt[i].f_fstypename : NULL;

#endif
}

static const char *
getblkdev (const char * path)
{
  char * c;
  char * dir;
  const char * device;

  dir = tr_strdup(path);

  for (;;)
    {
      device = getdev (dir);
      if (device != NULL)
        break;

      c = strrchr (dir, '/');
      if (c != NULL)
        *c = '\0';
      else
         break;
    }

  tr_free (dir);
  return device;
}

#if defined(__NetBSD__) && (__NetBSD_Version__ >= 600000000)
#include <quota.h>

static int64_t
getquota (const char * device)
{
  struct quotahandle *qh;
  struct quotakey qk;
  struct quotaval qv;
  int64_t limit;
  int64_t freespace;
  int64_t spaceused;

  qh = quota_open(device);
  if (qh == NULL) {
    return -1;
  }
  qk.qk_idtype = QUOTA_IDTYPE_USER;
  qk.qk_id = getuid();
  qk.qk_objtype = QUOTA_OBJTYPE_BLOCKS;
  if (quota_get(qh, &qk, &qv) == -1) {
    quota_close(qh);
    return -1;
  }
  if (qv.qv_softlimit > 0) {
    limit = qv.qv_softlimit;
  }
  else if (qv.qv_hardlimit > 0) {
    limit = qv.qv_hardlimit;
  }
  else {
    quota_close(qh);
    return -1;
  }
  spaceused = qv.qv_usage;
  quota_close(qh);

  freespace = limit - spaceused;
  return (freespace < 0) ? 0 : freespace;
}
#else
static int64_t
getquota (const char * device, int64_t * disk_used, int64_t * disk_soft, int64_t * disk_hard, int64_t * disk_timeleft)
{
  int64_t ret = -1;
  struct dqblk dq;

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
  if (quotactl(device, QCMD(Q_GETQUOTA, USRQUOTA), getuid(), (caddr_t) &dq) == 0)
    {
#elif defined(__sun)
  struct quotctl  op; 
  int fd = open(device, O_RDONLY); 
  if (fd < 0) 
    return ret; 
  op.op = Q_GETQUOTA; 
  op.uid = getuid(); 
  op.addr = (caddr_t) &dq; 
  if (ioctl(fd, Q_QUOTACTL, &op) == 0)
    {
      close(fd);
#else
  if (quotactl(QCMD(Q_GETQUOTA, USRQUOTA), device, getuid(), (caddr_t) &dq) == 0)
    {
#endif
      if (dq.dqb_bsoftlimit > 0 || dq.dqb_bhardlimit > 0)
        {
#if defined(__FreeBSD__) || defined(__OpenBSD__)
          *disk_used = (int64_t) dq.dqb_curblocks >> 1;
#elif defined(__APPLE__)
          *disk_used = (int64_t) dq.dqb_curbytes / 1024;
#elif defined(__UCLIBC__)
          *disk_used = (int64_t) btodb(dq.dqb_curblocks);
#elif defined(__sun) || (_LINUX_QUOTA_VERSION < 2)
          *disk_used = (int64_t) dq.dqb_curblocks >> 1;
#else
          *disk_used = btodb(dq.dqb_curspace);
#endif
          *disk_soft = dq.dqb_bsoftlimit;
          *disk_hard = dq.dqb_bhardlimit;
          if (dq.dqb_btime > (uint64_t)time(NULL))
            *disk_timeleft = (dq.dqb_btime - time(NULL)) / 3600;
          else
            *disk_timeleft = 0;
          ret = 0;
        }
    }
#if defined(__sun)
  close(fd);
#endif
  /* something went wrong */
  return ret;
}
#endif

#ifdef HAVE_XQM
static int64_t
getxfsquota (char * device, int64_t * disk_used, int64_t * disk_soft, int64_t * disk_hard, int64_t * disk_timeleft)
{
  int64_t ret = -1;
  int64_t limit;
  struct fs_disk_quota dq;

  if (quotactl(QCMD(Q_XGETQUOTA, USRQUOTA), device, getuid(), (caddr_t) &dq) == 0)
    {
      if (dq.d_blk_softlimit > 0 || dq.d_blk_hardlimit > 0)
        {
          *disk_used = dq.d_bcount >> 1;
          *disk_soft = dq.d_blk_softlimit >> 1;
          *disk_hard = dq.d_blk_hardlimit >> 1;
          if (dq.d_btimer > time(NULL))
            *disk_timeleft = (dq.d_btimer - time(NULL)) / 3600;
          else
            *disk_timeleft = 0;
          ret = 0;
        }
    }

  /* something went wrong */
  return ret;
}
#endif /* HAVE_XQM */
#endif /* _WIN32 */

static int64_t
tr_getQuotaSpace (const struct tr_device_info * info, int64_t * disk_used, int64_t * disk_soft, int64_t * disk_hard, int64_t * disk_timeleft)
{
  int64_t ret = -1;

#ifndef _WIN32

  if (info->fstype && !evutil_ascii_strcasecmp(info->fstype, "xfs"))
    {
#ifdef HAVE_XQM
      ret = getxfsquota (info->device, disk_used, disk_soft, disk_hard, disk_timeleft);
#endif
    }
  else
    {
      ret = getquota (info->device, disk_used, disk_soft, disk_hard, disk_timeleft);
    }
#endif /* _WIN32 */

  return ret;
}

static int64_t
tr_getDiskSpace (const char * path, int64_t * disk_used, int64_t * disk_soft, int64_t * disk_hard, int64_t * disk_timeleft)
{
#ifdef _WIN32

  ULARGE_INTEGER freeBytesAvailable = 0, totalNumberOfBytes = 0;
  if (GetDiskFreeSpaceEx (path, &freeBytesAvailable, &totalNumberOfBytes, NULL))
  {
    *disk_used = (totalNumberOfBytes.QuadPart - freeBytesAvailable.QuadPart) / 1024;
    *disk_hard = *disk_soft = totalNumberOfBytes.QuadPart / 1024;
    *disk_timeleft = 0;    
    return 0;
  }

#elif defined(HAVE_STATVFS)

  struct statvfs buf;
  if (!statvfs(path, &buf))
  {
    int64_t div = (int64_t)buf.f_bsize / 1024;
    *disk_used = (int64_t)(buf.f_blocks - buf.f_bavail) * div;
    *disk_soft = (int64_t)buf.f_blocks * div;
    *disk_hard = (int64_t)buf.f_blocks * div;
    *disk_timeleft = 0;
    return 0;
  }

#else

  #warning FIXME: not implemented

#endif
  return -1;
}

struct tr_device_info *
tr_device_info_create (const char * path)
{
  struct tr_device_info * info;

  info = tr_new0 (struct tr_device_info, 1);
  info->path = tr_strdup (path);
#ifndef _WIN32
  info->device = tr_strdup (getblkdev (path));
  info->fstype = tr_strdup (getfstype (path));
#endif

  return info;
}

void
tr_device_info_free (struct tr_device_info * info)
{
  if (info != NULL)
    {
      tr_free (info->fstype);
      tr_free (info->device);
      tr_free (info->path);
      tr_free (info);
    }
}

int64_t
tr_device_info_get_free_space (const struct tr_device_info * info)
{
  int64_t disk_used;
  int64_t disk_soft;
  int64_t disk_hard;
  int64_t disk_timeleft;
  int64_t free_space = -1;

  if (tr_device_info_get_space(info, &disk_used, &disk_soft, &disk_hard, &disk_timeleft) != -1)
    {
      if (disk_timeleft > 0) free_space = (disk_soft - disk_used) * 1024;
      else free_space = (disk_hard - disk_used) * 1024;
    }

  return free_space;
}

int64_t
tr_device_info_get_space (const struct tr_device_info * info, int64_t * disk_used, int64_t * disk_soft, int64_t * disk_hard, int64_t * disk_timeleft)
{
  int64_t ret;

  if ((info == NULL) || (info->path == NULL))
    {
      errno = EINVAL;
      ret = -1;
    }
  else
    {
      ret = tr_getQuotaSpace (info, disk_used, disk_soft, disk_hard, disk_timeleft);

      if (ret < 0)
        ret = tr_getDiskSpace (info->path, disk_used, disk_soft, disk_hard, disk_timeleft);
    }

  return ret;
}

/***
****
***/
