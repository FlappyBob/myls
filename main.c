#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define USERBUFSIZE 128
#define GROUPBUFSIZE 128
#define TIMEBUFSIZE 128
#define WRITEEND 1
#define READEND 0
#define BUFSIZE 128

static int err_code;
static int n_count = 0;
static int if_n = false;

void handle_error(char *fullname, char *action);
bool test_file(char *pathandname);
bool is_dir(char *pathandname);
const char *ftype_to_str(mode_t mode);
void list_file(char *pathandname, char *name, bool list_long, bool if_humanreadable);
void list_dir(char *dirname, bool list_long, bool list_all, bool recursive, bool if_humanreadable);

void hack()
{
    int status;
    // pipe from child to parent
    int c2p[2];
    pipe(c2p);

    // child
    if (fork() == 0)
    {
        // so instead of writing to terminal, it will write to the pipe.
        close(1);
        dup(c2p[WRITEEND]);
        close(c2p[READEND]);
        char *lala[] = {"/usr/bin/ls", NULL, NULL};
        execve("/usr/bin/ls", lala, 0);
        printf("exec failed!\n");
        exit(1);
    }
    // parent
    else
    {
        close(c2p[WRITEEND]);
        char buf[BUFSIZE];
        int n = read(c2p[READEND], buf, sizeof(buf));
        while (n)
        {
            write(1, buf, n);
            n = read(c2p[READEND], buf, sizeof(buf));
        }
        wait(&status);
    }
    exit(0);
}

#define NOT_YET_IMPLEMENTED(msg)                  \
    do                                            \
    {                                             \
        printf("Not yet implemented: " msg "\n"); \
        exit(255);                                \
    } while (0)

#define PRINT_ERROR(progname, what_happened, pathandname)               \
    do                                                                  \
    {                                                                   \
        printf("%s: %s %s: %s\n", progname, what_happened, pathandname, \
               strerror(errno));                                        \
    } while (0)

/* PRINT_PERM_CHAR:
 */
#define PRINT_PERM_CHAR(mode, mask, ch) printf("%s", (mode & mask) ? ch : "-");

/*
 * Get username for uid. Return 1 on failure, 0 otherwise.
 */
static int uname_for_uid(uid_t uid, char *buf, size_t buflen)
{
    struct passwd *p = getpwuid(uid);
    if (p == NULL)
    {
        handle_error(NULL, NULL);
        return 1;
    }
    strncpy(buf, p->pw_name, buflen);
    return 0;
}

/*
 * Get group name for gid. Return 1 on failure, 0 otherwise.
 */
static int group_for_gid(gid_t gid, char *buf, size_t buflen)
{
    struct group *g = getgrgid(gid);
    if (g == NULL)
    {
        return 1;
    }
    strncpy(buf, g->gr_name, buflen);
    return 0;
}

/*
 * Format the supplied `struct timespec` in `ts` (e.g., from `stat.st_mtime`) as a
 * string in `char *out`. Returns the length of the formatted string (see, `man
 * 3 strftime`).
 */
static size_t date_string(struct timespec *ts, char *out, size_t len)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    struct tm *t = localtime(&ts->tv_sec);
    if (now.tv_sec < ts->tv_sec)
    {
        // Future time, treat with care.
        return strftime(out, len, "%b %e %Y", t);
    }
    else
    {
        time_t difference = now.tv_sec - ts->tv_sec;
        if (difference < 31556952ull)
        {
            return strftime(out, len, "%b %e %H:%M", t);
        }
        else
        {
            return strftime(out, len, "%b %e %Y", t);
        }
    }
}

/*
 * Print help message and exit.
 */
static void help()
{
    // TODO: add to this
    printf("ls: List files\nUsage: ls [OPTION]... [FILE]...\n");
    printf("-a\tlist all files, including files starting with . and the pseudo-files . and ... \n");
    printf("-l\tuse a long listing format \n");
    printf("-n\tsuppresses the listing of files and instead produces a count of the number of files that would be printed if they were listed.\n");
    printf("-R\trecursively lists files in subdirectories\n");
    printf("-h\tprints “human readable”. With -l and -s, print sizes like 1K 234M 2G etc.\n");
    printf("--hack\tinvoke the system-supplied ls\n");
    printf("--help\tdisplay this help and exit\n");

    exit(0);
}

/*
 * call this when there's been an error.
 */
void handle_error(char *what_happened, char *fullname)
{
    // ignore the case when NULl is passed into the function.
    if (what_happened)
        PRINT_ERROR("ls", what_happened, fullname);
    // TODO: your code here: inspect errno and set err_code accordingly.
    /**
     * bit
     * 3: command-line was not found.
     * 4: denied access to a file or directory
     * 5: another type of error happened group_id function
     * 6: encounter a erronor
     * */
    err_code |= 64;
    switch (errno)
    {
    // not found
    case ENOENT:
        err_code |= 8;
        break;
    // denied
    case EACCES:
        err_code |= 16;
        break;
    // other error uname_for_uid error
    default:
        err_code |= 32;
        break;
    }

    return;
}

/*
 * test_file():
 * Use this to test for whether a file or dir exists
 */
bool test_file(char *pathandname)
{
    struct stat sb;
    if (stat(pathandname, &sb))
    {
        handle_error("cannot access", pathandname);
        return false;
    }
    return true;
}

/*
 * is_dir(): tests whether the argument refers to a directory.
 */
bool is_dir(char *pathandname)
{
    /* TODO: fillin */
    struct stat sb;
    stat(pathandname, &sb);
    return (sb.st_mode & S_IFMT) == S_IFDIR;
}

/* convert the mode field in a struct stat to a file type, for -l printing */
// order: permission links gid uid size modifiedtime name
const char *ftype_to_str(mode_t mode)
{
    const mode_t ori_mode = mode;
    char *permissionstr = malloc(sizeof(char) * 11);

    if (S_ISREG(mode))
    {
        permissionstr[0] = '-';
    }
    else if (S_ISDIR(mode))
    {
        permissionstr[0] = 'd';
    }
    else
    {
        permissionstr[0] = '?';
    }

    mode = ori_mode & S_IRWXU;
    permissionstr[1] = mode & S_IRUSR ? 'r' : '-';
    permissionstr[2] = mode & S_IWUSR ? 'w' : '-';
    permissionstr[3] = mode & S_IXUSR ? 'x' : '-';

    mode = ori_mode & S_IRWXG;
    permissionstr[4] = mode & S_IRGRP ? 'r' : '-';
    permissionstr[5] = mode & S_IWGRP ? 'w' : '-';
    permissionstr[6] = mode & S_IXGRP ? 'x' : '-';

    mode = ori_mode & S_IRWXO;
    permissionstr[7] = mode & S_IROTH ? 'r' : '-';
    permissionstr[8] = mode & S_IWOTH ? 'w' : '-';
    permissionstr[9] = mode & S_IXOTH ? 'x' : '-';
    permissionstr[10] = '\0';
    return permissionstr;
}

void human_readable(long int bytes, char *buf)
{
    memset(buf, '\0', 6);
    if (bytes < 1024)
        sprintf(buf, "%ld", bytes);
    else if (bytes < 1024 * 1024)
        sprintf(buf, "%.1lfK", (double)bytes / 1024);
    else if (bytes < 1024 * 1024 * 1024)
        sprintf(buf, "%.1lfM", (double)bytes / (1024 * 1024));
    else
        sprintf(buf, "%.1lfG", (double)bytes / (1024 * 1024 * 1024));
}

// get extension name of a file. 
char *extname(char *filename)
{
    char *dotptr;
    for (dotptr = filename + strlen(filename) - 1; *dotptr != '.' || dotptr == filename; dotptr--)
        if (dotptr == filename)
            return "";
    return dotptr + 1;
}

void list_file_long(char *pathandname, char *name, int if_humanreadable)
{
    struct timespec ts;
    struct stat sb;
    mode_t mode;
    char username[USERBUFSIZE];
    char groupname[GROUPBUFSIZE];
    char timebuf[TIMEBUFSIZE];

    if (stat(pathandname, &sb) == -1)
    {
        handle_error("cannot get the stat of", pathandname);
        return;
    }
    mode = sb.st_mode;
    const char *permissionstr = ftype_to_str(mode);
    printf("%4s %4ld", permissionstr, sb.st_nlink);

    // if we cannot get username successfully, handle error and print its id instead.
    if (uname_for_uid(sb.st_uid, username, USERBUFSIZE))
    {
        handle_error(NULL, pathandname);
        printf(" %5d", sb.st_uid);
    }
    else
    {
        printf(" %5s", username);
    }
    if (group_for_gid(sb.st_gid, groupname, GROUPBUFSIZE))
    {
        handle_error(NULL, pathandname);
        printf(" %5d", sb.st_gid);
    }
    else
    {
        printf(" %5s", groupname);
    }

    if (if_humanreadable)
    {
        char buf[6];
        human_readable(sb.st_size, buf);
        printf(" %7s", buf);
    }
    else
    {
        printf(" %7ld", sb.st_size);
    }

    time_t t = sb.st_mtime;
    ts.tv_sec = t;
    date_string(&ts, timebuf, TIMEBUFSIZE);
    printf(" %7s", timebuf);
    // free permission str

    // check if the filename ends with a .link sign
    if (strcmp(extname(name), "link") == 0)
    {
        int bufsiz = sb.st_size + 1;
        char *strbuf = malloc(bufsiz);
        // if reading link failure is met, handle error as other error and only print the name.
        int nbytes = readlink(name, strbuf, bufsiz);
        if (nbytes == -1)
        {
            handle_error(NULL, name);
            printf(" %-4s\n", name);
        }
        else
        {
            printf(" %s -> %.*s\n", name, (int)nbytes, strbuf);
        }
        free((void *)strbuf);
    }
    else
    {
        printf(" %-4s\n", name);
    }

    free((void *)permissionstr);
}

/* list_file():
 * implement the logic for listing a single file.
 */
void list_file(char *pathandname, char *name, bool list_long, bool if_humanreadable)
{
    // ignore if n is specified
    if (if_n)
    {
        n_count++;
        return;
    }
    // if dir but not "." and "..", add "/" to its name;
    if (is_dir(pathandname) && !(strcmp("..", name) == 0 || strcmp(".", name) == 0))
    {
        strcat(name, "/");
    }
    // if list long, print long;
    if (list_long)
    {
        list_file_long(pathandname, name, if_humanreadable);
    }
    else
    {
        printf("%s\n", name);
    }
}

/* list_dir():
 * implement the logic for listing a directory.
 */
void list_dir(char *dirname, bool list_long, bool list_all, bool recursive, bool if_humanreadable)
{
    if (recursive && !if_n)
    {
        printf("%s:\n", dirname);
    }

    char *pathandname;
    char *pathnameArr[100];
    int pathIndex = 0;

    DIR *dirp;
    struct dirent *dp;

    // if we cannot open, then return
    if ((dirp = opendir(dirname)) == NULL)
    {
        handle_error("cannot open", dirname);
        return;
    }

    while ((dp = readdir(dirp)) != NULL)
    {
        // does not list if -a flag is not specified
        if (!list_all && (dp->d_name[0] == '.' || strcmp(".", dp->d_name) == 0 || strcmp("..", dp->d_name) == 0))
        {
            continue;
        }
        pathandname = (char *)malloc(sizeof(dirname) + 3 + sizeof(dp->d_name));
        strcpy(pathandname, dirname);
        strcat(pathandname, "/");
        strcat(pathandname, dp->d_name);

        // if dp is directory & recursive, omit ".." and "." ,  append before list it and list call list_dir again
        if (recursive && is_dir(pathandname) && !(strcmp(".", dp->d_name) == 0 || strcmp("..", dp->d_name) == 0))
        {
            list_file(pathandname, dp->d_name, list_long, if_humanreadable);
            pathnameArr[pathIndex++] = pathandname;
        }
        // if dp is directory, append before list it.
        else if (is_dir(pathandname))
        {
            strcat(pathandname, "/");
            list_file(pathandname, dp->d_name, list_long, if_humanreadable);
            free(pathandname);
        }
        // if dp is file, just list it.
        else
        {
            list_file(pathandname, dp->d_name, list_long, if_humanreadable);
            free(pathandname);
        }
    }

    // prints out the listing subdirectories.
    for (int i = 0; i < pathIndex; i++)
    {
        list_dir(pathnameArr[i], list_long, list_all, recursive, if_humanreadable);
        free(pathnameArr[i]);
    }

    closedir(dirp);
    return;
}

int main(int argc, char *argv[])
{
    // This needs to be int since C does not specify whether char is signed or
    // unsigned.
    int opt;
    err_code = 0;
    bool if_humanreadable = false, list_long = false, list_all = false, list_recursive = false;
    // We make use of getopt_long for argument parsing, and this
    // (single-element) array is used as input to that function. The `struct
    // option` helps us parse arguments of the form `--FOO`. Refer to `man 3
    // getopt_long` for more information.
    struct option opts[] = {
        {.name = "help", .has_arg = no_argument, .flag = NULL, .val = '\a'},
        {.name = "hack", .has_arg = no_argument, .flag = NULL, .val = '\b'},
    };

    // This loop is used for argument parsing. Refer to `man 3 getopt_long` to
    // better understand what is going on here.
    while ((opt = getopt_long(argc, argv, "1alRnh", opts, NULL)) != -1)
    {
        switch (opt)
        {
        case '\a':
            // Handle the case that the user passed in `--help`. (In the
            // long argument array above, we used '\a' to indicate this
            // case.)
            help();
            break;
        case '\b':
            hack();
            break;
        case '1':
            // Safe to ignore since this is default behavior for our version
            // of ls.
            break;
        case 'a':
            list_all = true;
            break;
        case 'l':
            list_long = true;
            break;
        case 'R':
            list_recursive = true;
            break;
        case 'n':
            if_n = true;
            break;
        case 'h':
            if_humanreadable = true;
            break;
        default:
            printf("Unimplemented flag %d\n", opt);
            break;
        }
    }

    // If optind is smaller than argc there is still arguments, see if it is file or directories, and run those arguments with flags
    if (optind < argc)
    {
        for (int i = optind; i < argc; i++)
        {
            char *name = argv[i];
            if (!test_file(name))
                continue;
            if (is_dir(name))
            {
                // append path into dirname
                list_dir(name, list_long, list_all, list_recursive, if_humanreadable);
            }
            else
            {
                list_file(name, name, list_long, if_humanreadable);
            }
        }
    }
    // else, we run current directories with flags.
    else
    {
        list_dir(".", list_long, list_all, list_recursive, if_humanreadable);
    }
    // if if_n is turned on, display the count;
    if (if_n)
    {
        printf("%d\n", n_count);
    }

    exit(err_code);
}
