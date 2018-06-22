/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "3.0.4"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* Copy the first part of user declarations.  */
#line 1 "./exports.y" /* yacc.c:339  */

/*
 * UNFS3 exports parser and export controls
 * (C) 2004, Pascal Schmidt
 * see file LICENSE for license details
 */
#include "config.h"

#include <rpc/rpc.h>
#include <limits.h>

#ifdef WIN32
#include "winsupport.h"
#else
#include <netdb.h>
#include <syslog.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif /* WIN32 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nfs.h"
#include "mount.h"
#include "daemon.h"
#include "backend.h"
#include "exports.h"

#ifndef PATH_MAX
# define PATH_MAX	4096
#endif

/* for lack of a better place */
#ifdef __GNUC__
#define U(x) x __attribute__ ((unused))
#else
#define U(x) x
#endif

/* lexer stuff, to avoid compiler warnings */
int yylex(void);
extern FILE *yyin;

/*
 * C code used by yacc parser
 */

typedef struct {
	char		orig[NFS_MAXPATHLEN];
	int		options;
        char            password[PASSWORD_MAXLEN+1];
        uint32          password_hash;
	struct in_addr	addr;
	struct in_addr	mask;
	uint32		anonuid;
	uint32		anongid;
	struct e_host	*next;
} e_host;

typedef struct {
	char		path[NFS_MAXPATHLEN];
	char		orig[NFS_MAXPATHLEN];
	e_host		*hosts;
	uint32          fsid; /* export point fsid (for removables) */
	time_t          last_mtime; /* Last returned mtime (for removables) */
	uint32          dir_hash; /* Hash of dir contents (for removables) */
	struct e_item	*next;
} e_item;

/* export list, item, and host filled during parse */
static e_item *e_list = NULL;
static e_item cur_item;
static e_host cur_host;

/* last looked-up anonuid and anongid */
static uint32 last_anonuid = ANON_NOTSPECIAL;
static uint32 last_anongid = ANON_NOTSPECIAL;

/* mount protocol compatible variants */
static exports ne_list = NULL;
static struct exportnode ne_item;
static struct groupnode ne_host;

/* error status of last parse */
int e_error = FALSE;

/*
 * The FNV1a-32 hash algorithm
 * (http://www.isthe.com/chongo/tech/comp/fnv/)
 */
uint32 fnv1a_32(const char *str, uint32 hval)
{
    static const uint32 fnv_32_prime = 0x01000193;
    
    while (*str) {
	hval ^= *str++;
	hval *= fnv_32_prime;
    }
    return hval;
}

#ifdef WIN32
uint32 wfnv1a_32(const wchar_t *str, uint32 hval)
{
    static const uint32 fnv_32_prime = 0x01000193;
    
    while (*str) {
	hval ^= *str++;
	hval *= fnv_32_prime;
    }
    return hval;
}
#endif

/*
 * get static fsid, for use with removable media export points
 */
static uint32 get_free_fsid(const char *path)
{
    uint32 hval;

    /* The 32:th bit is set to one on all special filehandles. The
       last 31 bits are hashed from the export point path. */
    hval = fnv1a_32(path, 0);
    hval |= 0x80000000;
    return hval;
}


/*
 * clear current host
 */
static void clear_host(void)
{
	memset(&cur_host, 0, sizeof(e_host));
	strcpy(cur_host.orig, "<anon clnt>");
	memset(&ne_host, 0, sizeof(struct groupnode));
	
	cur_host.anonuid =
	cur_host.anongid = ANON_NOTSPECIAL; 
}

/*
 * clear current item
 */
static void clear_item(void)
{
	memset(&cur_item, 0, sizeof(e_item));
	memset(&ne_item, 0, sizeof(struct exportnode));
}

/*
 * add current host to current export item
 */
static void add_host(void)
{
	e_host *new;
	e_host *iter;
	
	groups ne_new;
	groups ne_iter;

	new = malloc(sizeof(e_host));
	ne_new = malloc(sizeof(struct groupnode));
	if (!new || !ne_new) {
		logmsg(LOG_EMERG, "out of memory, aborting");
		daemon_exit(CRISIS);
	}

	*new = cur_host;
	*ne_new = ne_host;
	ne_new->gr_name = new->orig;

	/* internal list */
	if (cur_item.hosts) {
		iter = cur_item.hosts;
		while (iter->next)
			iter = (e_host *) iter->next;
		iter->next = (struct e_host *) new;
	} else
		cur_item.hosts = new;

	/* matching mount protocol list */
	if (ne_item.ex_groups) {
		ne_iter = ne_item.ex_groups;
		while (ne_iter->gr_next)
			ne_iter = (groups) ne_iter->gr_next;
		ne_iter->gr_next = ne_new;
	} else
		ne_item.ex_groups = ne_new;
	
	clear_host();
}

/* 
   Normalize path, eliminating double slashes, etc. To be used instead
   of realpath, when realpath is not possible. Normalizing export
   points is important. Otherwise, mount requests might fail, since
   /x/y is not a prefix of ///x///y/ etc.
*/
char *normpath(const char *path, char *normpath)
{
	char *n;
	const char *p;

	/* Copy path to normpath, and replace blocks of slashes with
	   single slash */
	p = path;
	n = normpath;
	while (*p) {
		/* Skip over multiple slashes */
		if (*p == '/' && *(p+1) == '/') {
			p++;
			continue;
		}
		*n++ = *p++;
	}
	*n = '\0';

	/* Remove trailing slash, if any. */
	if ((n - normpath) > 1 && *(n-1) == '/')
		*(n-1) = '\0';

	return normpath;
}

/*
 * add current item to current export list
 */
static void add_item(const char *path)
{
	char buf[PATH_MAX];
	e_item *new;
	e_item *iter;
	e_host *host;
	/* Is this item marked as removable for all hosts? */
	int removable_for_all = 1;
	
	exports ne_new;
	exports ne_iter;

	new = malloc(sizeof(e_item));
	ne_new = malloc(sizeof(struct exportnode));
	if (!new || !ne_new) {
		logmsg(LOG_EMERG, "out of memory, aborting");
		daemon_exit(CRISIS);
	}

	/* Loop over all hosts and check if marked as removable. */
	host = cur_item.hosts;
	while (host) {
		if (!(host->options & OPT_REMOVABLE))
			removable_for_all = 0;
		host = (e_host *) host->next;
	}

	if (removable_for_all) {
		/* If marked as removable for all hosts, don't try
		   realpath. */
		normpath(path, buf);
	} else if (!backend_realpath(path, buf)) {
		logmsg(LOG_CRIT, "realpath for %s failed", path);
		e_error = TRUE;
		free(new);
		free(ne_new);
		clear_item();
		return;
	}

	if (strlen(buf) + 1 > NFS_MAXPATHLEN) {
		logmsg(LOG_CRIT, "attempted to export too long path");
		e_error = TRUE;
		free(new);
		free(ne_new);
		clear_item();
		return;
	}

	/* if no hosts listed, list default host */
	if (!cur_item.hosts)
		add_host();

	*new = cur_item;
	strcpy(new->path, buf);
	strcpy(new->orig, path);
	new->fsid = get_free_fsid(path);  
	new->last_mtime = 0;
	new->dir_hash = 0;

	*ne_new = ne_item;
	ne_new->ex_dir = new->orig;

	/* internal list */
	if (e_list) {
		iter = e_list;
		while (iter->next)
			iter = (e_item *) iter->next;
		iter->next = (struct e_item *) new;
	} else
		e_list = new;

	/* matching mount protocol list */
	if (ne_list) {
		ne_iter = ne_list;
		while (ne_iter->ex_next)
			ne_iter = (exports) ne_iter->ex_next;
		ne_iter->ex_next = ne_new;
	} else
		ne_list = ne_new;
		
	clear_item();
}

/*
 * fill current host's address given a hostname
 */
static void set_hostname(const char *name)
{
	struct hostent *ent;

	if (strlen(name) + 1 > NFS_MAXPATHLEN) {
		e_error = TRUE;
		return;
	}
	strcpy(cur_host.orig, name);

	ent = gethostbyname(name);

	if (ent) {
		memcpy(&cur_host.addr, ent->h_addr_list[0],
		       sizeof(struct in_addr));
		cur_host.mask.s_addr = 0;
		cur_host.mask.s_addr = ~cur_host.mask.s_addr;
	} else {
		logmsg(LOG_CRIT, "could not resolve hostname '%s'", name);
		e_error = TRUE;
	}
}	

/*
 * fill current host's address given an IP address
 */
static void set_ipaddr(const char *addr)
{
	strcpy(cur_host.orig, addr);
	
	if (!inet_aton(addr, &cur_host.addr))
		e_error = TRUE;
	cur_host.mask.s_addr = 0;
	cur_host.mask.s_addr = ~cur_host.mask.s_addr;
}

/*
 * compute network bitmask
 */
static unsigned long make_netmask(int bits) {
	unsigned long buf = 0;
	int i;

	for (i=0; i<bits; i++)
		buf = (buf << 1) + 1;
	for (; i < 32; i++)
		buf = (buf << 1);
	return htonl(buf);
}

/*
 * fill current host's address given IP address and netmask
 */
static void set_ipnet(char *addr, int new)
{
	char *pos, *net;

	pos = strchr(addr, '/');
	net = pos + 1;
	*pos = 0;
	
	set_ipaddr(addr);
	
	if (new)
		cur_host.mask.s_addr = make_netmask(atoi(net));
	else
		if (!inet_aton(net, &cur_host.mask))
			e_error = TRUE;

	*pos = '/';
	strcpy(cur_host.orig, addr);
}

/*
 * add an option bit to the current host
 */
static void add_option(const char *opt)
{
	if (strcmp(opt,"no_root_squash") == 0)
		cur_host.options |= OPT_NO_ROOT_SQUASH;
	else if (strcmp(opt,"root_squash") == 0)
		cur_host.options &= ~OPT_NO_ROOT_SQUASH;
	else if (strcmp(opt,"all_squash") == 0)
		cur_host.options |= OPT_ALL_SQUASH;
	else if (strcmp(opt,"no_all_squash") == 0)
		cur_host.options &= ~OPT_ALL_SQUASH;
	else if (strcmp(opt,"rw") == 0)
		cur_host.options |= OPT_RW;
	else if (strcmp(opt,"ro") == 0)
		cur_host.options &= ~OPT_RW;
	else if (strcmp(opt,"removable") == 0) {
		cur_host.options |= OPT_REMOVABLE;
	} else if (strcmp(opt,"fixed") == 0)
		cur_host.options &= ~OPT_REMOVABLE;
	else if (strcmp(opt,"insecure") == 0)
		cur_host.options |= OPT_INSECURE;
	else if (strcmp(opt,"secure") == 0)
		cur_host.options &= ~OPT_INSECURE;
	else
		logmsg(LOG_WARNING, "Warning: unknown exports option `%s' ignored",
			opt);
}

static void add_option_with_value(const char *opt, const char *val)
{
    if (strcmp(opt,"password") == 0) {
	if (strlen(val) > PASSWORD_MAXLEN) {
	    logmsg(LOG_WARNING, "Warning: password for export %s truncated to 64 chars",
		   cur_item.orig);
	}
	strncpy(cur_host.password, val, sizeof(password));
	cur_host.password[PASSWORD_MAXLEN] = '\0';
	/* Calculate hash */
	cur_host.password_hash = fnv1a_32(cur_host.password, 0);
    } else if (strcmp(opt,"anonuid") == 0) {
    	cur_host.anonuid = atoi(val);
    } else if (strcmp(opt,"anongid") == 0) {
    	cur_host.anongid = atoi(val);
    } else {
        logmsg(LOG_WARNING, "Warning: unknown exports option `%s' ignored",
            opt);
    }
}

/*
 * dummy error function
 */
void yyerror(U(char *s))
{
	e_error = TRUE;
	return;
}


#line 519 "y.tab.c" /* yacc.c:339  */

# ifndef YY_NULLPTR
#  if defined __cplusplus && 201103L <= __cplusplus
#   define YY_NULLPTR nullptr
#  else
#   define YY_NULLPTR 0
#  endif
# endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* In a future release of Bison, this section will be replaced
   by #include "y.tab.h".  */
#ifndef YY_YY_Y_TAB_H_INCLUDED
# define YY_YY_Y_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    PATH = 258,
    ID = 259,
    OPTVALUE = 260,
    WHITE = 261,
    IP = 262,
    NET = 263,
    OLDNET = 264
  };
#endif
/* Tokens.  */
#define PATH 258
#define ID 259
#define OPTVALUE 260
#define WHITE 261
#define IP 262
#define NET 263
#define OLDNET 264

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED

union YYSTYPE
{
#line 454 "./exports.y" /* yacc.c:355  */

	char text[NFS_MAXPATHLEN];

#line 581 "y.tab.c" /* yacc.c:355  */
};

typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;

int yyparse (void);

#endif /* !YY_YY_Y_TAB_H_INCLUDED  */

/* Copy the second part of user declarations.  */

#line 598 "y.tab.c" /* yacc.c:358  */

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif

#ifndef YY_ATTRIBUTE
# if (defined __GNUC__                                               \
      && (2 < __GNUC__ || (__GNUC__ == 2 && 96 <= __GNUC_MINOR__)))  \
     || defined __SUNPRO_C && 0x5110 <= __SUNPRO_C
#  define YY_ATTRIBUTE(Spec) __attribute__(Spec)
# else
#  define YY_ATTRIBUTE(Spec) /* empty */
# endif
#endif

#ifndef YY_ATTRIBUTE_PURE
# define YY_ATTRIBUTE_PURE   YY_ATTRIBUTE ((__pure__))
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# define YY_ATTRIBUTE_UNUSED YY_ATTRIBUTE ((__unused__))
#endif

#if !defined _Noreturn \
     && (!defined __STDC_VERSION__ || __STDC_VERSION__ < 201112)
# if defined _MSC_VER && 1200 <= _MSC_VER
#  define _Noreturn __declspec (noreturn)
# else
#  define _Noreturn YY_ATTRIBUTE ((__noreturn__))
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

#if defined __GNUC__ && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN \
    _Pragma ("GCC diagnostic push") \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")\
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif


#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYSIZE_T yynewbytes;                                            \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / sizeof (*yyptr);                          \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, (Count) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYSIZE_T yyi;                         \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  5
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   21

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  14
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  8
/* YYNRULES -- Number of rules.  */
#define YYNRULES  22
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  28

/* YYTRANSLATE[YYX] -- Symbol number corresponding to YYX as returned
   by yylex, with out-of-bounds checking.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   264

#define YYTRANSLATE(YYX)                                                \
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, without out-of-bounds checking.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      10,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      11,    12,     2,     2,    13,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9
};

#if YYDEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   469,   469,   470,   471,   475,   476,   477,   481,   482,
     483,   487,   488,   489,   493,   494,   495,   496,   500,   501,
     502,   506,   507
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || 0
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "PATH", "ID", "OPTVALUE", "WHITE", "IP",
  "NET", "OLDNET", "'\\n'", "'('", "')'", "','", "$accept", "exports",
  "export", "hosts", "host", "name", "opts", "opt", YY_NULLPTR
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
      10,    40,    41,    44
};
# endif

#define YYPACT_NINF -15

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-15)))

#define YYTABLE_NINF -1

#define yytable_value_is_error(Yytable_value) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int8 yypact[] =
{
      -2,     2,    10,    -8,    -4,   -15,    -2,   -15,   -15,   -15,
     -15,     7,   -15,     6,     3,   -15,     8,     4,     5,    -4,
       7,   -15,   -15,     7,   -15,     9,   -15,   -15
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       4,     5,     0,     2,     7,     1,     4,    14,    15,    16,
      17,    20,     6,     8,    11,     3,    21,     0,    18,    10,
      20,    22,    13,    20,     9,     0,    19,    12
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -15,    11,   -15,     0,   -15,   -15,   -14,   -15
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
      -1,     2,     3,    12,    13,    14,    17,    18
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_uint8 yytable[] =
{
       7,     1,     6,     8,     9,    10,    25,    11,     4,    26,
       5,    16,    19,    21,    20,     0,    22,    15,    23,    24,
       0,    27
};

static const yytype_int8 yycheck[] =
{
       4,     3,    10,     7,     8,     9,    20,    11,     6,    23,
       0,     4,     6,     5,    11,    -1,    12,     6,    13,    19,
      -1,    12
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,    15,    16,     6,     0,    10,     4,     7,     8,
       9,    11,    17,    18,    19,    15,     4,    20,    21,     6,
      11,     5,    12,    13,    17,    20,    20,    12
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    14,    15,    15,    15,    16,    16,    16,    17,    17,
      17,    18,    18,    18,    19,    19,    19,    19,    20,    20,
      20,    21,    21
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     3,     0,     1,     3,     2,     1,     3,
       2,     1,     4,     3,     1,     1,     1,     1,     1,     3,
       0,     1,     2
};


#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)
#define YYEMPTY         (-2)
#define YYEOF           0

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                  \
do                                                              \
  if (yychar == YYEMPTY)                                        \
    {                                                           \
      yychar = (Token);                                         \
      yylval = (Value);                                         \
      YYPOPSTACK (yylen);                                       \
      yystate = *yyssp;                                         \
      goto yybackup;                                            \
    }                                                           \
  else                                                          \
    {                                                           \
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;                                                  \
    }                                                           \
while (0)

/* Error token number */
#define YYTERROR        1
#define YYERRCODE       256



/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)

/* This macro is provided for backward compatibility. */
#ifndef YY_LOCATION_PRINT
# define YY_LOCATION_PRINT(File, Loc) ((void) 0)
#endif


# define YY_SYMBOL_PRINT(Title, Type, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Type, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*----------------------------------------.
| Print this symbol's value on YYOUTPUT.  |
`----------------------------------------*/

static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
{
  FILE *yyo = yyoutput;
  YYUSE (yyo);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
  YYUSE (yytype);
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyoutput, "%s %s (",
             yytype < YYNTOKENS ? "token" : "nterm", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yytype_int16 *yybottom, yytype_int16 *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yytype_int16 *yyssp, YYSTYPE *yyvsp, int yyrule)
{
  unsigned long int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       yystos[yyssp[yyi + 1 - yynrhs]],
                       &(yyvsp[(yyi + 1) - (yynrhs)])
                                              );
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
yystrlen (const char *yystr)
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            /* Fall through.  */
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return 1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return 2 if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYSIZE_T *yymsg_alloc, char **yymsg,
                yytype_int16 *yyssp, int yytoken)
{
  YYSIZE_T yysize0 = yytnamerr (YY_NULLPTR, yytname[yytoken]);
  YYSIZE_T yysize = yysize0;
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat. */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Number of reported tokens (one for the "unexpected", one per
     "expected"). */
  int yycount = 0;

  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yytoken != YYEMPTY)
    {
      int yyn = yypact[*yyssp];
      yyarg[yycount++] = yytname[yytoken];
      if (!yypact_value_is_default (yyn))
        {
          /* Start YYX at -YYN if negative to avoid negative indexes in
             YYCHECK.  In other words, skip the first -YYN actions for
             this state because they are default actions.  */
          int yyxbegin = yyn < 0 ? -yyn : 0;
          /* Stay within bounds of both yycheck and yytname.  */
          int yychecklim = YYLAST - yyn + 1;
          int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
          int yyx;

          for (yyx = yyxbegin; yyx < yyxend; ++yyx)
            if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR
                && !yytable_value_is_error (yytable[yyx + yyn]))
              {
                if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                  {
                    yycount = 1;
                    yysize = yysize0;
                    break;
                  }
                yyarg[yycount++] = yytname[yyx];
                {
                  YYSIZE_T yysize1 = yysize + yytnamerr (YY_NULLPTR, yytname[yyx]);
                  if (! (yysize <= yysize1
                         && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
                    return 2;
                  yysize = yysize1;
                }
              }
        }
    }

  switch (yycount)
    {
# define YYCASE_(N, S)                      \
      case N:                               \
        yyformat = S;                       \
      break
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
# undef YYCASE_
    }

  {
    YYSIZE_T yysize1 = yysize + yystrlen (yyformat);
    if (! (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
      return 2;
    yysize = yysize1;
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return 1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yyarg[yyi++]);
          yyformat += 2;
        }
      else
        {
          yyp++;
          yyformat++;
        }
  }
  return 0;
}
#endif /* YYERROR_VERBOSE */

/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
{
  YYUSE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}




/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;


/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    int yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       'yyss': related to states.
       'yyvs': related to semantic values.

       Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yytype_int16 yyssa[YYINITDEPTH];
    yytype_int16 *yyss;
    yytype_int16 *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    YYSIZE_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yyssp = yyss = yyssa;
  yyvsp = yyvs = yyvsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */
  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        YYSTYPE *yyvs1 = yyvs;
        yytype_int16 *yyss1 = yyss;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * sizeof (*yyssp),
                    &yyvs1, yysize * sizeof (*yyvsp),
                    &yystacksize);

        yyss = yyss1;
        yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yytype_int16 *yyss1 = yyss;
        union yyalloc *yyptr =
          (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
        if (! yyptr)
          goto yyexhaustedlab;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
                  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token.  */
  yychar = YYEMPTY;

  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 5:
#line 475 "./exports.y" /* yacc.c:1646  */
    { add_item((yyvsp[0].text)); }
#line 1682 "y.tab.c" /* yacc.c:1646  */
    break;

  case 6:
#line 476 "./exports.y" /* yacc.c:1646  */
    { add_item((yyvsp[-2].text)); }
#line 1688 "y.tab.c" /* yacc.c:1646  */
    break;

  case 7:
#line 477 "./exports.y" /* yacc.c:1646  */
    { add_item((yyvsp[-1].text)); }
#line 1694 "y.tab.c" /* yacc.c:1646  */
    break;

  case 11:
#line 487 "./exports.y" /* yacc.c:1646  */
    { add_host(); }
#line 1700 "y.tab.c" /* yacc.c:1646  */
    break;

  case 12:
#line 488 "./exports.y" /* yacc.c:1646  */
    { add_host(); }
#line 1706 "y.tab.c" /* yacc.c:1646  */
    break;

  case 13:
#line 489 "./exports.y" /* yacc.c:1646  */
    { add_host(); }
#line 1712 "y.tab.c" /* yacc.c:1646  */
    break;

  case 14:
#line 493 "./exports.y" /* yacc.c:1646  */
    { set_hostname((yyvsp[0].text)); }
#line 1718 "y.tab.c" /* yacc.c:1646  */
    break;

  case 15:
#line 494 "./exports.y" /* yacc.c:1646  */
    { set_ipaddr((yyvsp[0].text)); }
#line 1724 "y.tab.c" /* yacc.c:1646  */
    break;

  case 16:
#line 495 "./exports.y" /* yacc.c:1646  */
    { set_ipnet((yyvsp[0].text), TRUE); }
#line 1730 "y.tab.c" /* yacc.c:1646  */
    break;

  case 17:
#line 496 "./exports.y" /* yacc.c:1646  */
    { set_ipnet((yyvsp[0].text), FALSE); }
#line 1736 "y.tab.c" /* yacc.c:1646  */
    break;

  case 21:
#line 506 "./exports.y" /* yacc.c:1646  */
    { add_option((yyvsp[0].text)); }
#line 1742 "y.tab.c" /* yacc.c:1646  */
    break;

  case 22:
#line 507 "./exports.y" /* yacc.c:1646  */
    { add_option_with_value((yyvsp[-1].text),(yyvsp[0].text)); }
#line 1748 "y.tab.c" /* yacc.c:1646  */
    break;


#line 1752 "y.tab.c" /* yacc.c:1646  */
      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYEMPTY : YYTRANSLATE (yychar);

  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
# define YYSYNTAX_ERROR yysyntax_error (&yymsg_alloc, &yymsg, \
                                        yyssp, yytoken)
      {
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = YYSYNTAX_ERROR;
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == 1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = (char *) YYSTACK_ALLOC (yymsg_alloc);
            if (!yymsg)
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = 2;
              }
            else
              {
                yysyntax_error_status = YYSYNTAX_ERROR;
                yymsgp = yymsg;
              }
          }
        yyerror (yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYTERROR;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  yystos[yystate], yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#if !defined yyoverflow || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  yystos[*yyssp], yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  return yyresult;
}
#line 509 "./exports.y" /* yacc.c:1906  */


/*
 * C code using yacc parser + access code for exports list
 */

/* effective export list and access flag */
static e_item *export_list = NULL;
static volatile int exports_access = FALSE;

/* mount protocol compatible exports list */
exports exports_nfslist = NULL;

/*
 * free NFS groups list
 */
void free_nfsgroups(groups group)
{
	groups list, next;
	
	list = group;
	while (list) {
		next = (groups) list->gr_next;
		free(list);
		list = next;
	}
}

/*
 * free NFS exports list
 */
void free_nfslist(exports elist)
{
	exports list, next;

	list = elist;
	while(list) {
		next = (exports) list->ex_next;
		free_nfsgroups(list->ex_groups);
		free(list);
		list = next;
	}
}

/*
 * free list of host structures
 */
static void free_hosts(e_item *item)
{
	e_host *host, *cur;
	
	host = item->hosts;
	while (host) {
		cur = host;
		host = (e_host *) host->next;
		free(cur);
	}
}

/*
 * free list of export items
 */
static void free_list(e_item *item)
{
	e_item *cur;
	
	while (item) {
		free_hosts(item);
		cur = item;
		item = (e_item *) item->next;
		free(cur);
	}
}

/*
 * print out the current exports list (for debugging)
 */
void print_list(void)
{
	char addrbuf[16], maskbuf[16];

	e_item *item;
	e_host *host;
	
	item = e_list;
		
	while (item) {
		host = item->hosts;
		while (host) {
			/* inet_ntoa returns static buffer */
			strcpy(addrbuf, inet_ntoa(host->addr));
			strcpy(maskbuf, inet_ntoa(host->mask));
			printf("%s: ip %s mask %s options %i\n",
		       		item->path, addrbuf, maskbuf,
		       		host->options);
		       	host = (e_host *) host->next;
		}
		item = (e_item *) item->next;
	}
}

/*
 * clear current parse state
 */
static void clear_cur(void)
{
	e_list = NULL;
	ne_list = NULL;
	e_error = FALSE;
	clear_host();
	clear_item();
}

/*
 * parse an exports file
 */
int exports_parse(void)
{
	FILE *efile;

	/*
	 * if we are in the SIGHUP handler, a may_mount or get_options
	 * may currently be accessing the list
	 */
	if (exports_access) {
		logmsg(LOG_CRIT, "export list is being traversed, no reload\n");
		return FALSE;
	}

	efile = fopen(opt_exports, "r");
	if (!efile) {
		logmsg(LOG_CRIT, "could not open '%s', exporting nothing",
		       opt_exports);
		free_list(export_list);
		free_nfslist(exports_nfslist);
		export_list = NULL;
		exports_nfslist = NULL;
		return FALSE;
	}

	yyin = efile;
	clear_cur();
	yyparse();
	fclose(efile);
	
	if (e_error) {
		logmsg(LOG_CRIT, "syntax error in '%s', exporting nothing",
		       opt_exports);
		free_list(export_list);
		free_nfslist(exports_nfslist);
		export_list = NULL;
		exports_nfslist = NULL;
		return FALSE;
	}
	
	/* print out new list for debugging */
	if (!opt_detach)
		print_list();
	
	free_list(export_list);
	free_nfslist(exports_nfslist);
	export_list = e_list;
	exports_nfslist = ne_list;
	return TRUE;
}

/*
 * find a given host inside a host list, return options
 */
static e_host* find_host(struct in_addr remote, e_item *item,
		     char **password, uint32 *password_hash)
{
	e_host *host;

	host = item->hosts;
	while (host) {
		if ((remote.s_addr & host->mask.s_addr) == host->addr.s_addr) {
			if (password != NULL) 
				*password = host->password;
			if (password_hash != NULL)
     			        *password_hash = host->password_hash;
			return host;
		}
		host = (e_host *) host->next;
	}
	return NULL;
}

/* options cache */
int exports_opts = -1;
const char *export_path = NULL; 
uint32 export_fsid = 0;
uint32 export_password_hash = 0;

/*
 * given a path, return client's effective options
 */
int exports_options(const char *path, struct svc_req *rqstp,
		    char **password, uint32 *fsid)
{
	e_item *list;
	struct in_addr remote;
	unsigned int last_len = 0;
	
	exports_opts = -1;
	export_path = NULL;
	export_fsid = 0;
	last_anonuid = ANON_NOTSPECIAL;
	last_anongid = ANON_NOTSPECIAL;

	/* check for client attempting to use invalid pathname */
	if (!path || strstr(path, "/../"))
		return exports_opts;
	
	remote = get_remote(rqstp);

	/* protect against SIGHUP reloading the list */
	exports_access = TRUE;
	
	list = export_list;
	while (list) {
		/* longest matching prefix wins */
		if (strlen(list->path) > last_len    &&
#ifndef WIN32
		    strstr(path, list->path) == path) {
#else
		    !win_utf8ncasecmp(path, list->path, strlen(list->path))) {
#endif
		    e_host* cur_host = find_host(remote, list, password, &export_password_hash);

			if (fsid != NULL)
				*fsid = list->fsid;
			if (cur_host) {
				exports_opts = cur_host->options;
				export_path = list->path;
				export_fsid = list->fsid;
				last_len = strlen(list->path);
				last_anonuid = cur_host->anonuid;
				last_anongid = cur_host->anongid;
			}
		}
		list = (e_item *) list->next;
	}
	exports_access = FALSE;
	return exports_opts;
}

/*
 * check whether path is an export point
 */
int export_point(const char *path)
{
        e_item *list;

	exports_access = TRUE;
	list = export_list;

	while (list) {
	    if (strcmp(path, list->path) == 0) {
		exports_access = FALSE;
		return TRUE;
	    }
	    list = (e_item *) list->next;
	}
	exports_access = FALSE;
	return FALSE;
}

/*
 * return exported path from static fsid
 */
char *export_point_from_fsid(uint32 fsid, time_t **last_mtime, uint32 **dir_hash)
{
    e_item *list;
    
    exports_access = TRUE;
    list = export_list;
    
    while (list) {
	if (list->fsid == fsid) {
	    if (last_mtime != NULL)
		*last_mtime = &list->last_mtime;
	    if (dir_hash != NULL)
		*dir_hash = &list->dir_hash;
	    exports_access = FALSE;
	    return list->path;
	}
	list = (e_item *) list->next;
    }
    exports_access = FALSE;
    return NULL;
}


/*
 * check whether export options of a path match with last set of options
 */
nfsstat3 exports_compat(const char *path, struct svc_req *rqstp)
{
	int prev;
	uint32 prev_anonuid, prev_anongid;
	
	prev = exports_opts;
	prev_anonuid = last_anonuid;
	prev_anongid = last_anongid;
	
	if (exports_options(path, rqstp, NULL, NULL) == prev &&
	    last_anonuid == prev_anonuid &&
	    last_anongid == prev_anongid)
		return NFS3_OK;
	else if (exports_opts == -1)
		return NFS3ERR_ACCES;
	else
		return NFS3ERR_XDEV;
}

/*
 * check whether options indicate rw mount
 */
nfsstat3 exports_rw(void)
{
	if (exports_opts != -1 && (exports_opts & OPT_RW))
		return NFS3_OK;
	else
		return NFS3ERR_ROFS;
}

/*
 * returns the last looked-up anonuid for a mount (ANON_NOTSPECIAL means none in effect)
 */
uint32 exports_anonuid(void)
{
	return last_anonuid;
}

/*
 * returns the last looked-up anongid for a mount (ANON_NOTSPECIAL means none in effect)
 */
uint32 exports_anongid(void)
{
	return last_anongid;
}
