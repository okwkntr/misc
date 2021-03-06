/* cabextract 1.6 - a program to extract Microsoft Cabinet files
 * (C) 2000-2015 Stuart Caie <kyzer@4u.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* cabextract uses libmspack to access cabinet files. libmspack is
 * available from http://www.cabextract.org.uk/libmspack/
 */

#define _GNU_SOURCE 1

#if HAVE_CONFIG_H
#include <config.h>

#include <stdio.h> /* everyone has this! */

#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#if HAVE_CTYPE_H
# include <ctype.h>
#endif

#if HAVE_WCTYPE_H
# include <wctype.h>
#endif

#if HAVE_ERRNO_H
# include <errno.h>
#endif

#if HAVE_FNMATCH_H
# include <fnmatch.h>
#endif

#if HAVE_LIMITS_H
# include <limits.h>
#endif

#if HAVE_STDARG_H
# include <stdarg.h>
#endif

#if HAVE_STDLIB_H
# include <stdlib.h>
#endif

#if HAVE_STRING_H
# include <string.h>
#endif

#if HAVE_STRINGS_H
# include <strings.h>
#endif

#if HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#if HAVE_UTIME || HAVE_UTIMES
# if HAVE_UTIME_H
#  include <utime.h>
# else
#  include <sys/utime.h>
# endif
#endif

#if HAVE_DIRENT_H
# include <dirent.h>
#else
# define dirent direct
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#if !STDC_HEADERS
# if !HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif
# if !HAVE_STRCASECMP
#  define strcasecmp strcmpi
# endif
# if !HAVE_MEMCPY
#  define memcpy(d,s,n) bcopy((s),(d),(n))
# endif
# if !HAVE_MEMMOVE
#  define memmove(d,s,n) bcopy((s),(d),(n))
# endif
#endif

#if HAVE_MKDIR
# if MKDIR_TAKES_ONE_ARG
#  define mkdir(a, b) mkdir(a)
# endif
#else
# if HAVE__MKDIR
#  define mkdir(a, b) _mkdir(a)
# else
#  error "Don't know how to create a directory on this system."
# endif
#endif

#ifndef HAVE_MKTIME
extern time_t mktime(struct tm *tp);
#endif

#ifndef FNM_CASEFOLD
# define FNM_CASEFOLD (0)
#endif

#include "getopt.h"

#endif

#include <mspack.h>
#include <md5.h>

/* structures and global variables */
struct option optlist[] = {
  { "directory", 1, NULL, 'd' },
  { "fix",       0, NULL, 'f' },
  { "filter",    1, NULL, 'F' },
  { "help",      0, NULL, 'h' },
  { "list",      0, NULL, 'l' },
  { "lowercase", 0, NULL, 'L' },
  { "pipe",      0, NULL, 'p' },
  { "quiet",     0, NULL, 'q' },
  { "single",    0, NULL, 's' },
  { "test",      0, NULL, 't' },
  { "version",   0, NULL, 'v' },
  { "stdin-fname",   0, NULL, 'n' },
  { NULL,        0, NULL, 0   }
};

struct file_mem {
  struct file_mem *next;
  dev_t st_dev;
  ino_t st_ino; 
  char *from;
};

struct cabextract_args {
  int help, lower, pipe, view, quiet, single, fix, test;
  char *dir, *filter, *stdin_fname;
};

/* global variables */
struct mscab_decompressor *cabd = NULL;

struct file_mem *cab_args = NULL;
struct file_mem *cab_exts = NULL;
struct file_mem *cab_seen = NULL;

mode_t user_umask;

struct cabextract_args args = {
  0, 0, 0, 0, 0, 0, 0, 0,
  NULL, NULL, NULL
};

int cabxbuf_open(FILE *fh);
void cabxbuf_close();
int cabxbuf_read(void *, int);
int cabxbuf_seek(off_t, int);
int cabxbuf_tell();

#ifdef DEBUG
#define debug printf
#else
#define debug 1 ? (void) 0 : printf
#endif /* DEBUG */
#define DATA_SIZE 256
#define IS_STDIN(fname) (strncmp((fname), "/dev/stdin", 10) == 0 || \
                         strncmp((fname), "-", 1) == 0)

typedef struct cabx_data {
  char data[DATA_SIZE];
  size_t len;
  struct cabx_data *next;
} cabx_data_t;

typedef struct cabx_buf {
  cabx_data_t *head;
  cabx_data_t *current;
  size_t size;
  off_t offset;
} cabx_buf_t;

cabx_buf_t g_cabxbuf = {0};
/* okawa */

/** A special filename. Extracting to this filename will send the output
 * to standard output instead of a file on disk. The magic happens in
 * cabx_open() when the STDOUT_FNAME pointer is given as a filename, so
 * treat this like a constant rather than a string.
 */
const char *STDOUT_FNAME = "stdout";

/** A special filename. Extracting to this filename will send the output
 * through an MD5 checksum calculator, instead of a file on disk. The
 * magic happens in cabx_open() when the TEST_FNAME pointer is given as a
 * filename, so treat this like a constant rather than a string. 
 */

const char *TEST_FNAME = "test";

/** A global MD5 context, used when a file is written to TEST_FNAME */
struct md5_ctx md5_context;

/** The resultant MD5 checksum, used when a file is written to TEST_FNAME */
unsigned char md5_result[16];

/* prototypes */
static int process_cabinet(char *cabname);

static void load_spanning_cabinets(struct mscabd_cabinet *basecab,
                                   char *basename);
static char *find_cabinet_file(char *origcab, char *cabname);
static struct mscabd_cabinet *find_cabinet_from_list(
    struct mscabd_cabinet *basecab, char *basename, 
    int reverse_flag);
static int unix_path_seperators(struct mscabd_file *files);
static char *create_output_name(const char *fname, const char *dir,
                                int lower, int isunix, int unicode);
static void set_date_and_perm(struct mscabd_file *file, char *filename);

static void memorise_file(struct file_mem **fml, char *name, char *from);
static int recall_file(struct file_mem *fml, char *name, char **from);
static void forget_files(struct file_mem **fml);
static int ensure_filepath(char *path);
static char *cab_error(struct mscab_decompressor *cd);

static struct mspack_file *cabx_open(struct mspack_system *this,
                                     const char *filename, int mode);
static void cabx_close(struct mspack_file *file);
static int cabx_read(struct mspack_file *file, void *buffer, int bytes);
static int cabx_write(struct mspack_file *file, void *buffer, int bytes);
static int cabx_seek(struct mspack_file *file, off_t offset, int mode);
static off_t cabx_tell(struct mspack_file *file);
static void cabx_msg(struct mspack_file *file, const char *format, ...);
static void *cabx_alloc(struct mspack_system *this, size_t bytes);
static void cabx_free(void *buffer);
static void cabx_copy(void *src, void *dest, size_t bytes);

/**
 * A cabextract-specific implementation of mspack_system that allows
 * the NULL filename to be opened for writing as a synonym for writing
 * to stdout.
 */
static struct mspack_system cabextract_system = {
  &cabx_open, &cabx_close, &cabx_read,  &cabx_write, &cabx_seek,
  &cabx_tell, &cabx_msg, &cabx_alloc, &cabx_free, &cabx_copy, NULL
};

int main(int argc, char *argv[]) {
  int i, err;

  /* parse options */
  while ((i = getopt_long(argc, argv, "d:fF:hlLpqstvn:", optlist, NULL)) != -1) {
    switch (i) {
    case 'd': args.dir    = optarg; break;
    case 'f': args.fix    = 1;      break;
    case 'F': args.filter = optarg; break;
    case 'h': args.help   = 1;      break;
    case 'l': args.view   = 1;      break;
    case 'L': args.lower  = 1;      break;
    case 'p': args.pipe   = 1;      break;
    case 'q': args.quiet  = 1;      break;
    case 's': args.single = 1;      break;
    case 't': args.test   = 1;      break;
    case 'v': args.view   = 1;      break;
    case 'n': args.stdin_fname = optarg; break;
    }
  }

  if (args.help) {
    fprintf(stderr,
      "Usage: %s [options] [-d dir] <cabinet file(s)>\n\n"
      "This will extract all files from a cabinet or executable cabinet.\n"
      "For multi-part cabinets, only specify the first file in the set.\n\n",
      argv[0]);
    fprintf(stderr,
      "Options:\n"
      "  -v   --version     print version / list cabinet\n"
      "  -h   --help        show this help page\n"
      "  -l   --list        list contents of cabinet\n"
      "  -t   --test        test cabinet integrity\n"
      "  -q   --quiet       only print errors and warnings\n"
      "  -L   --lowercase   make filenames lowercase\n"
      "  -f   --fix         fix (some) corrupted cabinets\n");
    fprintf(stderr,
      "  -p   --pipe        pipe extracted files to stdout\n"
      "  -s   --single      restrict search to cabs on the command line\n"
      "  -F   --filter      extract only files that match the given pattern\n"
      "  -d   --directory   extract all files to the given directory\n\n"
      "  -n   --stdin-fname name of cabfile which from stdin\n\n"
      "cabextract %s (C) 2000-2011 Stuart Caie <kyzer@4u.net>\n"
      "This is free software with ABSOLUTELY NO WARRANTY.\n",
      VERSION);
    return EXIT_FAILURE;
  }

  if (args.test && args.view) {
    fprintf(stderr, "%s: You cannot use --test and --list at the same time.\n"
            "Try '%s --help' for more information.\n", argv[0], argv[0]);
    return EXIT_FAILURE;
  }

  if (optind == argc) {
    /* no arguments other than the options */
    if (args.view) {
      printf("cabextract(custom) version %s\n", VERSION);
      return 0;
    }
    else {
      fprintf(stderr, "%s: No cabinet files specified.\nTry '%s --help' "
              "for more information.\n", argv[0], argv[0]);
      return EXIT_FAILURE;
    }
  }

  /* memorise command-line cabs if necessary */
  if (args.single) {
    for (i = optind; i < argc; i++) memorise_file(&cab_args, argv[i], NULL);
  }

  /* extracting to stdout implies shutting up on stdout */
  if (args.pipe && !args.view) args.quiet = 1;

  /* open libmspack */
  MSPACK_SYS_SELFTEST(err);
  if (err) {
    if (err == MSPACK_ERR_SEEK) {
      fprintf(stderr,
              "FATAL ERROR: libmspack is compiled for %d-bit file IO,\n"
              "             cabextract is compiled for %d-bit file IO.\n",
              (sizeof(off_t) == 4) ? 64 : 32,
              (sizeof(off_t) == 4) ? 32 : 64);
    }
    else {
      fprintf(stderr, "FATAL ERROR: libmspack self-test returned %d\n", err);
    }
    return EXIT_FAILURE;
  }

  if (!(cabd = mspack_create_cab_decompressor(&cabextract_system))) {
    fprintf(stderr, "can't create libmspack CAB decompressor\n");
    return EXIT_FAILURE;
  }

  /* obtain user's umask */
  user_umask = umask(0);
  umask(user_umask);

  /* turn on/off 'fix MSZIP' mode */
  cabd->set_param(cabd, MSCABD_PARAM_FIXMSZIP, args.fix);

  /* process cabinets */
  for (i = optind, err = 0; i < argc; i++) {
    err += process_cabinet(argv[i]);
  }

  /* error summary */
  if (!args.quiet) {
    if (err) printf("\nAll done, errors in processing %d file(s)\n", err);
    else printf("\nAll done, no errors.\n");
  }

  /* close libmspack */
  mspack_destroy_cab_decompressor(cabd);

  /* empty file-memory lists */
  forget_files(&cab_args);
  forget_files(&cab_exts);
  forget_files(&cab_seen);

  /* close stdin buffer */
  cabxbuf_close();

  return err ? EXIT_FAILURE : EXIT_SUCCESS;
}

/**
 * Processes each file argument on the command line, as specified by the
 * command line options. This does the main bulk of work in cabextract.
 *
 * @param basename the file to process
 * @return the number of files with errors, usually 0 for success or 1 for
 *         failure
 */
static int process_cabinet(char *basename) {
  struct mscabd_cabinet *basecab, *cab, *cab2;
  struct mscabd_file *file;
  int isunix, fname_offset, viewhdr = 0;
  char *from, *name;
  int errors = 0;

  /* do not process repeat cabinets */
  if (recall_file(cab_seen, basename, &from) ||
      recall_file(cab_exts, basename, &from)) {
    if (!args.quiet) {
      if (!from) printf("%s: skipping known cabinet\n", basename);
      else printf("%s: skipping known cabinet (from %s)\n", basename, from);
    }
    return 0; /* return success */
  }
  memorise_file(&cab_seen, basename, NULL);

  /* search the file for cabinets */
  if (!(basecab = cabd->search(cabd, basename))) {
    if (cabd->last_error(cabd)) {
      fprintf(stderr, "%s: %s\n", basename, cab_error(cabd));
    }
    else {
      fprintf(stderr, "%s: no valid cabinets found\n", basename);
    }
    return 1;
  }

  /* iterate over all cabinets found in that file */
  for (cab = basecab; cab; cab = cab->next) {

    /* load all spanning cabinets */
    load_spanning_cabinets(cab, basename);

    /* determine whether UNIX or MS-DOS path seperators are used */
    isunix = unix_path_seperators(cab->files);

    /* print headers */
    if (!viewhdr) {
      if (args.view) {
        if (!args.quiet) {
          printf("Viewing cabinet: %s\n", basename);
          printf(" File size | Date       Time     | Name\n");
          printf("-----------+---------------------+-------------\n");
        }
      }
      else {
        if (!args.quiet) {
          printf("%s cabinet: %s\n", args.test ? "Testing" : "Extracting",
                                     basename);
        }
      }
      viewhdr = 1;
    }

    /* the full UNIX output filename includes the output
     * directory. However, for filtering purposes, we don't want to 
     * include that. So, we work out where the filename part of the 
     * output name begins. This is the same for every extracted file.
     */
    if (args.filter) {
      fname_offset = args.dir ? (strlen(args.dir) + 1) : 0;
    }

    /* process all files */
    for (file = cab->files; file; file = file->next) {
      /* create the full UNIX output filename */
      if (!(name = create_output_name(file->filename, args.dir,
            args.lower, isunix, file->attribs & MSCAB_ATTRIB_UTF_NAME)))
      {
        errors++;
        continue;
      }

      /* if filtering, do so now. skip if file doesn't match filter */
      if (args.filter &&
          fnmatch(args.filter, &name[fname_offset], FNM_CASEFOLD))
      {
        free(name);
        continue;
      }

      /* view, extract or test the file */
      if (args.view) {
        if (args.quiet) {
          printf("%s\n", name);
        } else {
          printf("%10u | %02d.%02d.%04d %02d:%02d:%02d | %s\n",
                 file->length, file->date_d, file->date_m, file->date_y,
                 file->time_h, file->time_m, file->time_s, name);
        }
      }
      else if (args.test) {
        if (cabd->extract(cabd, file, TEST_FNAME)) {
          /* file failed to extract */
          printf("  %s  failed (%s)\n", name, cab_error(cabd));
          errors++;
        }
        else {
          /* file extracted OK, print the MD5 checksum in md5_result. Print
           * the checksum right-aligned to 79 columns if that's possible,
           * otherwise just print it 2 spaces after the filename and "OK" */

          /* "  filename  OK  " is 8 chars + the length of filename,
           * the MD5 checksum itself is 32 chars. */
          int spaces = 79 - (strlen(name) + 8 + 32);
          printf("  %s  OK  ", name);
          while (spaces-- > 0) putchar(' ');
          printf("%02x%02x%02x%02x%02x%02x%02x%02x"
                 "%02x%02x%02x%02x%02x%02x%02x%02x\n",
                 md5_result[0], md5_result[1], md5_result[2], md5_result[3],
                 md5_result[4], md5_result[5], md5_result[6], md5_result[7],
                 md5_result[8], md5_result[9], md5_result[10],md5_result[11],
                 md5_result[12],md5_result[13],md5_result[14],md5_result[15]);
        }
      }
      else {
        /* extract the file */
        if (args.pipe) {
          /* extracting to stdout */
          if (cabd->extract(cabd, file, STDOUT_FNAME)) {
            fprintf(stderr, "%s(%s): %s\n", STDOUT_FNAME, name,
                                            cab_error(cabd));
            errors++;
          }
        }
        else {
          /* extracting to a regular file */
          if (!args.quiet) printf("  extracting %s\n", name);

          if (!ensure_filepath(name)) {
            fprintf(stderr, "%s: can't create file path\n", name);
            errors++;
          }
          else {
            if (cabd->extract(cabd, file, name)) {
              fprintf(stderr, "%s: %s\n", name, cab_error(cabd));
              errors++;
            }
            else {
              set_date_and_perm(file, name);
            }
          }
        }
      }
      free(name);
    } /* for (all files in cab) */

    /* free the spanning cabinet filenames [not freed by cabd->close()] */
    if (!IS_STDIN(basename)) {
      for (cab2 = cab->prevcab; cab2; cab2 = cab2->prevcab) free((void*)cab2->filename);
      for (cab2 = cab->nextcab; cab2; cab2 = cab2->nextcab) free((void*)cab2->filename);
    }
  } /* for (all cabs) */

  /* free all loaded cabinets */
  cabd->close(cabd, basecab);
  return errors;
}

/**
 * Follows the spanning cabinet chain specified in a cabinet, loading
 * and attaching the spanning cabinets as it goes.
 *
 * @param basecab  the base cabinet to start the chain from.
 * @param basename the full pathname of the base cabinet, so spanning
 *                 cabinets can be found in the same path as the base cabinet.
 * @see find_cabinet_file()
 */
static void load_spanning_cabinets(struct mscabd_cabinet *basecab,
                                   char *basename)
{
  struct mscabd_cabinet *cab, *cab2;
  char *name, *tname;

  /* load any spanning cabinets -- backwards */
  tname = args.stdin_fname;
  for (cab = basecab; cab->flags & MSCAB_HDR_PREVCAB; cab = cab->prevcab) {
    printf("prevname=%s\n", cab->prevname);
    if (IS_STDIN(basename)) {
      /* REVISIT: 1st arg "basecab" shuld be first cab(=search while file) */
      if (!tname || !(cab2 = find_cabinet_from_list(basecab, tname, 1))) {
        fprintf(stderr, "%s: can't find %s\n", basename, cab->prevname);
        break;
      }
      name = cab->prevname;
      /* prevcab always basecab? */
      //tname = cab->prevname;
    } else {
      if (!(name = find_cabinet_file(basename, cab->prevname))) {
        fprintf(stderr, "%s: can't find %s\n", basename, cab->prevname);
        break;
      }
    }
    if (args.single && !recall_file(cab_args, name, NULL)) break;
    if (!args.quiet) {
      printf("%s: extends backwards to %s (%s)\n", basename,
             cab->prevname, cab->previnfo);
    }
    if (IS_STDIN(basename)) { 
      if (!cab2 || cabd->prepend(cabd, cab, cab2)) {
        fprintf(stderr, "%s: can't prepend %s: %s\n", basename,
                cab->prevname, cab_error(cabd));
        break;
      }
      /* remove cab2 from list */
      if (cab2->next) cab2->next->prev = cab2->prev;
      cab2->prev->next = cab2->next;
    } else if (!(cab2 = cabd->open(cabd,name)) || cabd->prepend(cabd, cab, cab2)) {
      fprintf(stderr, "%s: can't prepend %s: %s\n", basename,
              cab->prevname, cab_error(cabd));
      if (cab2) cabd->close(cabd, cab2);
      break;
    }
    memorise_file(&cab_exts, name, basename);
  }

  /* load any spanning cabinets -- forwards */
  tname = args.stdin_fname;
  for (cab = basecab; cab->flags & MSCAB_HDR_NEXTCAB; cab = cab->nextcab) {
    if (IS_STDIN(basename)) {
      /* REVISIT: 1st arg "basecab" shuld be first cab(=search while file) */
      if (!tname || !(cab2 = find_cabinet_from_list(basecab, tname, 0))) {
        fprintf(stderr, "%s: can't find %s\n", basename, cab->nextname);
        break;
      }
      name = cab->nextname;
      /* prevcab always basecab? */
      //tname = cab->nextname;
    } else {
      if (!(name = find_cabinet_file(basename, cab->nextname))) {
        fprintf(stderr, "%s: can't find %s\n", basename, cab->nextname);
        break;
      }
    }
    if (args.single && !recall_file(cab_args, name, NULL)) break;
    if (!args.quiet) {
      printf("%s: extends to %s (%s)\n", basename,
             cab->nextname, cab->nextinfo);
    }
    if (IS_STDIN(basename)) {
      if (!cab2 || cabd->append(cabd, cab, cab2)) {
        fprintf(stderr, "%s: can't append %s: %s\n", basename,
                cab->prevname, cab_error(cabd));
        break;
      }
      /* remove cab2 from list */
      if (cab2->next) cab2->next->prev = cab2->prev;
      cab2->prev->next = cab2->next;
    } else if (!(cab2 = cabd->open(cabd,name)) || cabd->append(cabd, cab, cab2)) {
      fprintf(stderr, "%s: can't append %s: %s\n", basename,
              cab->nextname, cab_error(cabd));
      if (cab2) cabd->close(cabd, cab2);
      break;
    }
    memorise_file(&cab_exts, name, basename);
  }
}

/**
 * Matches a cabinet's filename case-insensitively in the filesystem and
 * returns the case-correct form.
 *
 * @param origcab if this is non-NULL, the pathname part of this filename
 *                will be extracted, and the search will be conducted in
 *                that directory.
 * @param cabname the internal CAB filename to search for.
 * @return a copy of the full, case-correct filename of the given cabinet
 *         filename, or NULL if the specified filename does not exist on disk.
 */
static char *find_cabinet_file(char *origcab, char *cabname) {
  struct dirent *entry;
  struct stat st_buf;
  int found = 0, len;
  char *tail, *cab;
  DIR *dir;

  /* ensure we have a cabinet name at all */
  if (!cabname || !cabname[0]) return NULL;

  /* find if there's a directory path in the origcab */
  tail = origcab ? strrchr(origcab, '/') : NULL;
  len = (tail - origcab) + 1;

  /* allocate memory for our copy */
  if (!(cab = malloc((tail ? len : 2) + strlen(cabname) + 1))) return NULL;

  /* add the directory path from the original cabinet name, or "." */
  if (tail) memcpy(cab, origcab, (size_t) len);
  else      cab[0]='.', cab[1]='/', len=2;
  cab[len] = '\0';

  /* try accessing the cabinet with its current name (case-sensitive) */
  strcpy(&cab[len], cabname);
  if (stat(cab, &st_buf) == 0) {
    found = 1;
  }
  else {
    /* cabinet was not found, look for it in the current dir */
    cab[len] = '\0';
    if ((dir = opendir(cab))) {
      while ((entry = readdir(dir))) {
        if (strcasecmp(cabname, entry->d_name) == 0) {
          strcat(cab, entry->d_name);
          found = (stat(cab, &st_buf) == 0);
          break;
        }
      }
      closedir(dir);
    }
  }

  if (!found || !S_ISREG(st_buf.st_mode)) {
    /* cabinet not found, or not a regular file */
    free(cab);
    cab = NULL;
  }

  return cab;
}

static struct mscabd_cabinet *find_cabinet_from_list(
    struct mscabd_cabinet *basecab, char *basename, 
    int reverse_flag)
{
  struct mscabd_cabinet *cab;
  for (cab = basecab; cab; cab = cab->next) {
    if (reverse_flag && cab->flags & MSCAB_HDR_NEXTCAB) {
      if (strncmp(cab->nextname, basename, strlen(cab->nextname)) == 0) {
        return cab;
      }
    } else if (!reverse_flag && cab->flags & MSCAB_HDR_PREVCAB){
      if (strncmp(cab->prevname, basename, strlen(cab->prevname)) == 0) {
        return cab;
      }
    }
  }
  return NULL;
}

/**
 * Determines whether UNIX '/' or MS-DOS '\' path seperators are used in
 * the cabinet file. The algorithm is as follows:
 *
 * Look at all slashes in all filenames. If there are no slashes, MS-DOS
 * seperators are assumed (it doesn't matter). If all are backslashes,
 * MS-DOS seperators are assumed. If all are forward slashes, UNIX
 * seperators are assumed.
 *
 * If not all slashes are the same, go through each filename, looking for
 * the first slash.  If the part of the filename up to and including the
 * slash matches the previous filename, that kind of slash is the
 * directory seperator.
 *
 * @param files list of files in the cab file
 * @return 0 for MS-DOS seperators, or 1 for UNIX seperators.
 */
static int unix_path_seperators(struct mscabd_file *files) {
  struct mscabd_file *fi;
  char slash=0, backslash=0, *oldname;
  int oldlen;

  for (fi = files; fi; fi = fi->next) {
    char *p;
    for (p = fi->filename; *p; p++) {
      if (*p == '/') slash = 1;
      if (*p == '\\') backslash = 1;
    }
    if (slash && backslash) break;
  }

  if (slash) {
    /* slashes, but no backslashes = UNIX */
    if (!backslash) return 1;
  }
  else {
    /* no slashes = MS-DOS */
    return 0;
  }

  /* special case if there's only one file - just take the first slash */
  if (!files->next) {
    char c, *p = fi->filename;
    while ((c = *p++)) {
      if (c == '\\') return 0; /* backslash = MS-DOS */
      if (c == '/')  return 1; /* slash = UNIX */
    }
    /* should not happen - at least one slash was found! */
    return 0;
  }

  oldname = NULL;
  oldlen = 0;
  for (fi = files; fi; fi = fi->next) {
    char *name = fi->filename;
    int len = 0;
    while (name[len]) {
      if ((name[len] == '\\') || (name[len] == '/')) break;
      len++;
    }
    if (!name[len]) len = 0; else len++;

    if (len && (len == oldlen)) {
      if (strncmp(name, oldname, (size_t) len) == 0)
        return (name[len-1] == '\\') ? 0 : 1;
    }
    oldname = name;
    oldlen = len;
  }

  /* default */
  return 0;
}

/**
 * Creates a UNIX filename from the internal CAB filename and the given
 * parameters.
 *
 * @param fname  the internal CAB filename.
 * @param dir    a directory path to prepend to the output filename.
 * @param lower  if non-zero, filename should be made lower-case.
 * @param isunix if zero, MS-DOS path seperators are used in the internal
 *               CAB filename. If non-zero, UNIX path seperators are used.
 * @param utf8   if non-zero, the internal CAB filename is encoded in UTF-8.
 * @return a freshly allocated and created filename, or NULL if there was
 *         not enough memory.
 * @see unix_path_seperators()
 */
static char *create_output_name(const char *fname, const char *dir,
				int lower, int isunix, int utf8)
{
  char sep   = (isunix) ? '/'  : '\\'; /* the path-seperator */
  char slash = (isunix) ? '\\' : '/';  /* the other slash */

  size_t dirlen = dir ? strlen(dir) + 1 : 0; /* length of dir + '/' */
  size_t filelen = strlen(fname);

  /* worst case, UTF-8 processing expands all chars to 4 bytes */
  char *name =  malloc(dirlen + (filelen * 4) + 2);

  unsigned char *i    = (unsigned char *) &fname[0];
  unsigned char *iend = (unsigned char *) &fname[filelen];
  unsigned char *o    = (unsigned char *) &name[dirlen], c;

  if (!name) {
    fprintf(stderr, "Can't allocate output filename\n");
    return NULL;
  }

  /* copy directory prefix if needed */ 
  if (dir) {
    strcpy(name, dir);
    name[dirlen - 1] = '/';
  }

  /* copy cab filename to output name, converting MS-DOS slashes to UNIX
   * slashes as we go. Also lowercases characters if needed. */
  if (utf8) {
    /* handle UTF-8 encoded filenames (see RFC 3629). This doesn't reject bad
     * UTF-8 with overlong encodings, but does re-encode it as valid UTF-8. */
    while (i < iend) {
      /* get next UTF-8 character */
      int x;
      if ((c = *i++) < 0x80) {
        x = c;
      }
      else if (c >= 0xC2 && c < 0xE0 && i <= iend && (i[0] & 0xC0) == 0x80) {
        x = (c & 0x1F) << 6;
        x |= *i++ & 0x3F;
      }
      else if (c >= 0xE0 && c < 0xF0 && i+1 <= iend && (i[0] & 0xC0) == 0x80 &&
               (i[1] & 0xC0) == 0x80)
      {
        x = (c & 0x0F) << 12;
        x |= (*i++ & 0x3F) << 6;
        x |= *i++ & 0x3F;
      }
      else if (c >= 0xF0 && c < 0xF5 && i+2 <= iend && (i[0] & 0xC0) == 0x80 &&
               (i[1] & 0xC0) == 0x80 && (i[2] & 0xC0) == 0x80)
      {
        x = (c & 0x07) << 18;
        x |= (*i++ & 0x3F) << 12;
        x |= (*i++ & 0x3F) << 6;
        x |= *i++ & 0x3F;
      }
      else {
        x = 0xFFFD; /* unicode replacement character */
      }

      if (x <= 0 || x > 0x10FFFF) {
        x = 0xFFFD; /* invalid code point or cheeky null byte */
      }

#ifdef HAVE_TOWLOWER
      if (lower) x = towlower(x);
#else
      if (lower && x < 256) x = tolower(x);
#endif

      /* whatever is the path separator -> '/'
       * whatever is the other slash    -> '\' */
      if (x == sep) x = '/'; else if (x == slash) x = '\\';

      /* convert unicode character back to UTF-8 */
      if (x < 0x80) {
        *o++ = (unsigned char) x;
      }
      else if (x < 0x800) {
        *o++ = 0xC0 | (x >> 6);
        *o++ = 0x80 | (x & 0x3F);
      }
      else if (x < 0x10000) {
        *o++ = 0xE0 | (x >> 12);
        *o++ = 0x80 | ((x >> 6) & 0x3F);
        *o++ = 0x80 | (x & 0x3F);
      }
      else if (x <= 0x10FFFF) {
        *o++ = 0xF0 | (x >> 18);
        *o++ = 0x80 | ((x >> 12) & 0x3F);
        *o++ = 0x80 | ((x >> 6) & 0x3F);
        *o++ = 0x80 | (x & 0x3F);
      }
      else {
        *o++ = 0xEF; /* unicode replacement character in UTF-8 */
        *o++ = 0xBF;
        *o++ = 0xBD;
      }
    }
  }
  else {
    /* non UTF-8 version */
    while (i < iend) {
      c = *i++;
      if (lower) c = (unsigned char) tolower((int) c);
      if (c == sep) c = '/'; else if (c == slash) c = '\\';
      *o++ = c;
    }
  }
  *o++ = '\0';

  /* remove any leading slashes in the cab filename part.
   * This prevents unintended absolute file path access. */
  o = (unsigned char *) &name[dirlen];
  for (i = o; *i == '/' || *i == '\\'; i++);
  if (i != o) {
    size_t len = strlen((char *) i);
    if (len > 0) {
      memmove(o, i, len + 1);
    }
    else {
      /* change filename composed entirely of leading slashes to "x" */
      strcpy((char *) o, "x");
    }
  }

  /* search for "../" or "..\" in cab filename part and change to "xx"
   * This prevents unintended directory traversal. */
  for (; *o; o++) {
    if ((o[0] == '.') && (o[1] == '.') && (o[2] == '/' || o[2] == '\\')) {
      o[0] = o[1] = 'x';
      o += 2;
    }
  }

  return name;
}

/**
 * Sets the last-modified time and file permissions on a file.
 *
 * @param file     the internal CAB file whose date, time and attributes will 
 *                 be used.
 * @param filename the name of the UNIX file whose last-modified time and
 *                 file permissions will be set.
 */
static void set_date_and_perm(struct mscabd_file *file, char *filename) {
  mode_t mode;
  struct tm tm;
#if HAVE_UTIME
  struct utimbuf utb;
#elif HAVE_UTIMES
  struct timeval tv[2];
#endif

  /* set last modified date */
  tm.tm_sec   = file->time_s;
  tm.tm_min   = file->time_m;
  tm.tm_hour  = file->time_h;
  tm.tm_mday  = file->date_d;
  tm.tm_mon   = file->date_m - 1;
  tm.tm_year  = file->date_y - 1900;
  tm.tm_isdst = -1;

#if HAVE_UTIME
  utb.actime = utb.modtime = mktime(&tm);
  utime(filename, &utb);
#elif HAVE_UTIMES
  tv[0].tv_sec  = tv[1].tv_sec  = mktime(&tm);
  tv[0].tv_usec = tv[1].tv_usec = 0;
  utimes(filename, &tv[0]);
#endif

  /* set permissions */
  mode = 0444;
  if (  file->attribs & MSCAB_ATTRIB_EXEC)    mode |= 0111;
  if (!(file->attribs & MSCAB_ATTRIB_RDONLY)) mode |= 0222;
  chmod(filename, mode & ~user_umask);
}

/* ------- support functions ------- */

/**
 * Memorises a file by its device and inode number rather than its name. If
 * the file does not exist, it will not be memorised.
 *
 * @param fml  address of the file_mem list that will memorise this file.
 * @param name name of the file to memorise.
 * @param from a string that, if not NULL, will be duplicated stored with
 *             the memorised file.
 * @see recall_file(), forget_files()
 */
static void memorise_file(struct file_mem **fml, char *name, char *from) {
  struct file_mem *fm;
  struct stat st_buf;
  if (stat(name, &st_buf) != 0) return;
  if (!(fm = malloc(sizeof(struct file_mem)))) return;
  fm->st_dev = st_buf.st_dev;
  fm->st_ino = st_buf.st_ino;
  fm->from = (from) ? malloc(strlen(from)+1) : NULL;
  if (fm->from) strcpy(fm->from, from);
  fm->next = *fml;
  *fml = fm;
}

/**
 * Determines if a file has been memorised before, by its device and inode
 * number. If the file does not exist, it cannot be recalled.
 *
 * @param fml  list to search for previously memorised file
 * @param name name of file to recall.
 * @param from if non-NULL, this is an address that the associated "from"
 *             description pointer will be stored.
 * @return non-zero if the file has been previously memorised, zero if the
 *         file is unknown or does not exist.
 * @see memorise_file(), forget_files()
 */
static int recall_file(struct file_mem *fml, char *name, char **from) {
  struct file_mem *fm;
  struct stat st_buf;
  if (stat(name, &st_buf) != 0) return 0;
  for (fm = fml; fm; fm = fm->next) {
    if ((st_buf.st_ino == fm->st_ino) && (st_buf.st_dev == fm->st_dev)) {
      if (from) *from = fm->from; return 1;
    }
  }
  return 0;
}

/**
 * Frees all memory used by a file_mem list.
 *
 * @param fml address of the list to free
 * @see memorise_file()
 */
static void forget_files(struct file_mem **fml) {
  struct file_mem *fm, *next;
  for (fm = *fml; fm; fm = next) {
    next = fm->next;
    free(fm->from);
    free(fm);
  }
  *fml = NULL;
}

/**
 * Ensures that all directory components in a filepath exist. New directory
 * components are created, if necessary.
 *
 * @param path the filepath to check
 * @return non-zero if all directory components in a filepath exist, zero
 *         if components do not exist and cannot be created
 */
static int ensure_filepath(char *path) {
  struct stat st_buf;
  char *p;
  int ok;

  for (p = &path[1]; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    ok = (stat(path, &st_buf) == 0) && S_ISDIR(st_buf.st_mode);
    if (!ok) ok = (mkdir(path, 0777 & ~user_umask) == 0);
    *p = '/';
    if (!ok) return 0;
  }
  return 1;
}

/**
 * Returns a string with an error message appropriate for the last error
 * of the CAB decompressor.
 *
 * @param  cd the CAB decompressor.
 * @return a constant string with an appropriate error message.
 */
static char *cab_error(struct mscab_decompressor *cd) {
  switch (cd->last_error(cd)) {
  case MSPACK_ERR_OPEN:
  case MSPACK_ERR_READ:
  case MSPACK_ERR_WRITE:
  case MSPACK_ERR_SEEK:
    return strerror(errno);
  case MSPACK_ERR_NOMEMORY:
    return "out of memory";
  case MSPACK_ERR_SIGNATURE:
    return "bad CAB signature";
  case MSPACK_ERR_DATAFORMAT:
    return "error in CAB data format";
  case MSPACK_ERR_CHECKSUM:
    return "checksum error";
  case MSPACK_ERR_DECRUNCH:
    return "decompression error";
  }
  return "unknown error";
}

struct mspack_file_p {
  FILE *fh;
  const char *name;
  char regular_file;
};

static struct mspack_file *cabx_open(struct mspack_system *this,
                                    const char *filename, int mode)
{
  struct mspack_file_p *fh;
  const char *fmode;

  /* Use of the STDOUT_FNAME pointer for a filename means the file should
   * actually be extracted to stdout. Use of the TEST_FNAME pointer for a
   * filename means the file should only be MD5-summed.
   */
  if (filename == STDOUT_FNAME || filename == TEST_FNAME) {
    /* only WRITE mode is valid for these special files */
    if (mode != MSPACK_SYS_OPEN_WRITE) {
      return NULL;
    }
  }

  /* ensure that mode is one of READ, WRITE, UPDATE or APPEND */
  switch (mode) {
  case MSPACK_SYS_OPEN_READ:   fmode = "rb";  break;
  case MSPACK_SYS_OPEN_WRITE:  fmode = "wb";  break;
  case MSPACK_SYS_OPEN_UPDATE: fmode = "r+b"; break;
  case MSPACK_SYS_OPEN_APPEND: fmode = "ab";  break;
  default: return NULL;
  }

  debug("open:%s,%d\n", filename, IS_STDIN(filename));
  if ((fh = malloc(sizeof(struct mspack_file_p)))) {
    fh->name = filename;

    if (filename == STDOUT_FNAME) {
      fh->regular_file = 0;
      fh->fh = stdout;
      return (struct mspack_file *) fh;
    }
    else if (filename == TEST_FNAME) {
      fh->regular_file = 0;
      fh->fh = NULL;
      md5_init_ctx(&md5_context);
      return (struct mspack_file *) fh;
    }
    else if (IS_STDIN(filename)) {
      fh->regular_file = 0;
      fh->fh = stdin;
      if (cabxbuf_open(fh->fh) == 0) {
        return (struct mspack_file *) fh;
      }
    }
    else {
      /* regular file - simply attempt to open it */
      fh->regular_file = 1;
      if ((fh->fh = fopen(filename, fmode))) {
        return (struct mspack_file *) fh;
      }
    }
    /* error - free file handle and return NULL */
    free(fh);
  }
  return NULL;
}

static void cabx_close(struct mspack_file *file) {
  struct mspack_file_p *this = (struct mspack_file_p *) file;
  if (this) {
    if (this->name == TEST_FNAME) {
      md5_finish_ctx(&md5_context, (void *) &md5_result);
    } 
    else if (this->regular_file) {
      fclose(this->fh);
    }
    free(this);
  }
}

static int cabx_read(struct mspack_file *file, void *buffer, int bytes) {
  struct mspack_file_p *this = (struct mspack_file_p *) file;
  if (this && IS_STDIN(this->name)){
    return cabxbuf_read(buffer, bytes);
  }
  if (this && this->regular_file && buffer && bytes >= 0) {
    size_t count = fread(buffer, 1, (size_t) bytes, this->fh);
    if (!ferror(this->fh)) return (int) count;

  }
  return -1;
}

static int cabx_write(struct mspack_file *file, void *buffer, int bytes) {
  struct mspack_file_p *this = (struct mspack_file_p *) file;
  if (this && buffer && bytes >= 0) {
    if (this->name == TEST_FNAME) {
      md5_process_bytes(buffer, (size_t) bytes, &md5_context);
      return bytes;
    }
    else {
      /* regular files and the stdout writer */
      size_t count = fwrite(buffer, 1, (size_t) bytes, this->fh);
      if (!ferror(this->fh)) return (int) count;
    }
  }
  return -1;
}

static int cabx_seek(struct mspack_file *file, off_t offset, int mode) {
  struct mspack_file_p *this = (struct mspack_file_p *) file;
  if (this && IS_STDIN(this->name)) {
    return cabxbuf_seek(offset, mode);
  }
  if (this && this->regular_file) {
    switch (mode) {
    case MSPACK_SYS_SEEK_START: mode = SEEK_SET; break;
    case MSPACK_SYS_SEEK_CUR:   mode = SEEK_CUR; break;
    case MSPACK_SYS_SEEK_END:   mode = SEEK_END; break;
    default: return -1;
    }
#if HAVE_FSEEKO
    return fseeko(this->fh, offset, mode);
#else
    return fseek(this->fh, offset, mode);
#endif
  }
  return -1;
}

static off_t cabx_tell(struct mspack_file *file) {
  struct mspack_file_p *this = (struct mspack_file_p *) file;
  if (this && IS_STDIN(this->name)) {
    return cabxbuf_tell();
  }
#if HAVE_FSEEKO
  return (this && this->regular_file) ? (off_t) ftello(this->fh) : 0;
#else
  return (this && this->regular_file) ? (off_t) ftell(this->fh) : 0;
#endif
}

static void cabx_msg(struct mspack_file *file, const char *format, ...) {
  va_list ap;
  if (file) {
    fprintf(stderr, "%s: ", ((struct mspack_file_p *) file)->name);
  }
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  fputc((int) '\n', stderr);
  fflush(stderr);
}
static void *cabx_alloc(struct mspack_system *this, size_t bytes) {
  return malloc(bytes);
}
static void cabx_free(void *buffer) {
  free(buffer);
  buffer = NULL;
}
static void cabx_copy(void *src, void *dest, size_t bytes) {
  memcpy(dest, src, bytes);
}

int
cabxbuf_open(FILE *fh)
{
  size_t total = 0;
  cabx_data_t *data, *pdata;

  if (g_cabxbuf.head != NULL)
    return 0;

  for(;;) {
    data = malloc(sizeof(cabx_data_t));
    if (data == NULL) {
      cabxbuf_close();
      return -1;
    }
    memset(data, 0, sizeof(cabx_data_t));
    data->len = fread(data->data, 1, DATA_SIZE, fh);

    debug("cabxbuf_open: len=%d\n", data->len);

    total += data->len;
    if (g_cabxbuf.head == NULL)
      g_cabxbuf.head = data;
    else 
      pdata->next = data;
    if (data->len < DATA_SIZE)
      break;
    pdata = data;
  }
  g_cabxbuf.size = total;
  g_cabxbuf.offset = 0;
  g_cabxbuf.current = g_cabxbuf.head;
  return 0;
}

void
cabxbuf_close()
{
  if (g_cabxbuf.head == NULL)
    return;

  cabx_data_t *data, *next;
  data = g_cabxbuf.head;
  for(;;) {
    if (data->next == NULL)
      break;
    next = data->next;
    free(data);
    data = next;
  }
}

int
cabxbuf_read(void *buf, int bytes)
{
  volatile off_t current;
  int tmpsize = bytes, readsize, total = 0;
  void *pos = buf;
  cabx_data_t *data = g_cabxbuf.current;

  current = g_cabxbuf.offset % DATA_SIZE;
  debug("read start:current=%d, data='%s'\n", 
      (int)current, (char *)data->data);

  while (tmpsize > 0 && data != NULL) {
    if (current + tmpsize >= data->len) {
      readsize = data->len - current;
    } else {
      readsize = tmpsize < data->len ? tmpsize : data->len;
    }
    memcpy(pos, (void *)data->data + current, (size_t)readsize);
    if (current + tmpsize >= data->len) {
      current = 0;
      data = data->next;
    }
    tmpsize -= readsize;
    pos += readsize;
    total += readsize;
  }
  g_cabxbuf.offset += total;
  g_cabxbuf.current = data;
  debug("read end:%d:%d:%d\n", bytes, tmpsize, total);
  return total;
}

int
cabxbuf_seek(off_t offset, int mode)
{
  off_t count, rest;
  cabx_data_t *data;
  switch (mode) {
    case SEEK_SET: 
      break;
    case SEEK_CUR:
      offset += g_cabxbuf.offset;
      break;
    case SEEK_END:
      offset = g_cabxbuf.size;
      break;
    default: return -1;
  }
  if (offset <= 0) {
    offset = 0;
    data = g_cabxbuf.head;
  } else if (offset >= g_cabxbuf.size) {
    offset = g_cabxbuf.size;
    data = g_cabxbuf.head;
    while (data->next != NULL)
      data = data->next;
  } else {
    count = offset / DATA_SIZE;
    data = g_cabxbuf.head;

    while (count-- > 0)
      data = data->next;
  }
  g_cabxbuf.current = data;
  g_cabxbuf.offset = offset;
  return 0;
}

int
cabxbuf_tell()
{
  return g_cabxbuf.offset;
}
