/** \ingroup popt
 * \file popt/popt.c
 */

/* (C) 1998-2002 Red Hat, Inc. -- Licensing details are in the COPYING
   file accompanying popt source distributions, available from
   ftp://ftp.rpm.org/pub/rpm/dist */

#undef	MYDEBUG

#include "system.h"

#if defined(__LCLINT__)
/*@-declundef -exportheader @*/
extern long long int strtoll(const char *nptr, /*@null@*/ char **endptr,
		int base)
	/*@modifies *endptr@*/;
/*@=declundef =exportheader @*/
#endif

#ifdef HAVE_FLOAT_H
#include <float.h>
#endif
#include <math.h>
#include <sys/types.h>
#include <unistd.h>

#include "poptint.h"

#ifdef	MYDEBUG
/*@unchecked@*/
int _popt_debug = 0;
#endif

#if 0//!defined(HAVE_STRERROR) && !defined(__LCLINT__)
static char * strerror(int errno)
{
    extern int sys_nerr;
    extern char * sys_errlist[];

    if ((0 <= errno) && (errno < sys_nerr))
	return sys_errlist[errno];
    else
	return POPT_("unknown errno");
}
#endif

#ifdef MYDEBUG
/*@unused@*/
static void prtcon(const char *msg, poptContext con)
{
    if (msg) fprintf(stderr, "%s", msg);
    fprintf(stderr, "\tcon %p os %p nextCharArg \"%s\" nextArg \"%s\" argv[%d] \"%s\"\n",
	con, con->os,
	(con->os->nextCharArg ? con->os->nextCharArg : ""),
	(con->os->nextArg ? con->os->nextArg : ""),
	con->os->next,
	(con->os->argv && con->os->argv[con->os->next]
		? con->os->argv[con->os->next] : ""));
}
#endif

void poptSetExecPath(poptContext con, const char * path, int allowAbsolute)
{
    con->execPath = _free(con->execPath);
    con->execPath = xstrdup(path);
    con->execAbsolute = allowAbsolute;
    return;
}

static void invokeCallbacksPRE(poptContext con, const struct poptOption * opt)
	/*@globals internalState@*/
	/*@modifies internalState@*/
{
    if (opt != NULL)
    for (; opt->longName || opt->shortName || opt->arg; opt++) {
	poptArg arg = { .ptr = opt->arg };
	if (arg.ptr)
	switch (poptArgType(opt)) {
	case POPT_ARG_INCLUDE_TABLE:	/* Recurse on included sub-tables. */
	    poptSubstituteHelpI18N(arg.opt);	/* XXX side effects */
	    invokeCallbacksPRE(con, arg.opt);
	    /*@switchbreak@*/ break;
	case POPT_ARG_CALLBACK:		/* Perform callback. */
	    if (!CBF_ISSET(opt, PRE))
		/*@switchbreak@*/ break;
/*@-noeffectuncon @*/	/* XXX no known way to annotate (*vector) calls. */
	    arg.cb(con, POPT_CALLBACK_REASON_PRE, NULL, NULL, opt->descrip);
/*@=noeffectuncon @*/
	    /*@switchbreak@*/ break;
	}
    }
}

static void invokeCallbacksPOST(poptContext con, const struct poptOption * opt)
	/*@globals internalState@*/
	/*@modifies internalState@*/
{
    if (opt != NULL)
    for (; opt->longName || opt->shortName || opt->arg; opt++) {
	poptArg arg = { .ptr = opt->arg };
	if (arg.ptr)
	switch (poptArgType(opt)) {
	case POPT_ARG_INCLUDE_TABLE:	/* Recurse on included sub-tables. */
	    poptSubstituteHelpI18N(arg.opt);	/* XXX side effects */
	    invokeCallbacksPOST(con, arg.opt);
	    /*@switchbreak@*/ break;
	case POPT_ARG_CALLBACK:		/* Perform callback. */
	    if (!CBF_ISSET(opt, POST))
		/*@switchbreak@*/ break;
/*@-noeffectuncon @*/	/* XXX no known way to annotate (*vector) calls. */
	    arg.cb(con, POPT_CALLBACK_REASON_POST, NULL, NULL, opt->descrip);
/*@=noeffectuncon @*/
	    /*@switchbreak@*/ break;
	}
    }
}

static void invokeCallbacksOPTION(poptContext con,
				const struct poptOption * opt,
				const struct poptOption * myOpt,
				/*@null@*/ const void * myData, int shorty)
	/*@globals internalState@*/
	/*@modifies internalState@*/
{
    const struct poptOption * cbopt = NULL;
    poptArg cbarg = { .ptr = NULL };

    if (opt != NULL)
    for (; opt->longName || opt->shortName || opt->arg; opt++) {
	poptArg arg = { .ptr = opt->arg };
	switch (poptArgType(opt)) {
	case POPT_ARG_INCLUDE_TABLE:	/* Recurse on included sub-tables. */
	    poptSubstituteHelpI18N(arg.opt);	/* XXX side effects */
	    if (opt->arg != NULL)
		invokeCallbacksOPTION(con, opt->arg, myOpt, myData, shorty);
	    /*@switchbreak@*/ break;
	case POPT_ARG_CALLBACK:		/* Save callback info. */
	    if (CBF_ISSET(opt, SKIPOPTION))
		/*@switchbreak@*/ break;
	    cbopt = opt;
	    cbarg.ptr = opt->arg;
	    /*@switchbreak@*/ break;
	default:		/* Perform callback on matching option. */
	    if (cbopt == NULL || cbarg.cb == NULL)
		/*@switchbreak@*/ break;
	    if ((myOpt->shortName && opt->shortName && shorty &&
			myOpt->shortName == opt->shortName)
	     || (myOpt->longName != NULL && opt->longName != NULL &&
			!strcmp(myOpt->longName, opt->longName)))
	    {	const void *cbData = (cbopt->descrip ? cbopt->descrip : myData);
/*@-noeffectuncon @*/	/* XXX no known way to annotate (*vector) calls. */
		cbarg.cb(con, POPT_CALLBACK_REASON_OPTION,
			myOpt, con->os->nextArg, cbData);
/*@=noeffectuncon @*/
		/* Terminate (unless explcitly continuing). */
		if (!CBF_ISSET(cbopt, CONTINUE))
		    return;
	    }
	    /*@switchbreak@*/ break;
	}
    }
}

poptContext poptGetContext(const char * name, int argc, const char ** argv,
			const struct poptOption * options, unsigned int flags)
{
    poptContext con = malloc(sizeof(*con));

    if (con == NULL) return NULL;	/* XXX can't happen */
    memset(con, 0, sizeof(*con));

    con->os = con->optionStack;
    con->os->argc = argc;
/*@-dependenttrans -assignexpose@*/	/* FIX: W2DO? */
    con->os->argv = argv;
/*@=dependenttrans =assignexpose@*/
    con->os->argb = NULL;

    if (!(flags & POPT_CONTEXT_KEEP_FIRST))
	con->os->next = 1;			/* skip argv[0] */

    con->leftovers = calloc( (size_t)(argc + 1), sizeof(*con->leftovers) );
/*@-dependenttrans -assignexpose@*/	/* FIX: W2DO? */
    con->options = options;
/*@=dependenttrans =assignexpose@*/
    con->aliases = NULL;
    con->numAliases = 0;
    con->flags = flags;
    con->execs = NULL;
    con->numExecs = 0;
    con->finalArgvAlloced = argc * 2;
    con->finalArgv = calloc( (size_t)con->finalArgvAlloced, sizeof(*con->finalArgv) );
    con->execAbsolute = 1;
    con->arg_strip = NULL;

    if (getenv("POSIXLY_CORRECT") || getenv("POSIX_ME_HARDER"))
	con->flags |= POPT_CONTEXT_POSIXMEHARDER;

    if (name)
	con->appName = xstrdup(name);

    invokeCallbacksPRE(con, con->options);

    return con;
}

static void cleanOSE(/*@special@*/ struct optionStackEntry *os)
	/*@uses os @*/
	/*@releases os->nextArg, os->argv, os->argb @*/
	/*@modifies os @*/
{
    os->nextArg = _free(os->nextArg);
    os->argv = _free(os->argv);
    os->argb = PBM_FREE(os->argb);
}

void poptResetContext(poptContext con)
{
    int i;

    if (con == NULL) return;
    while (con->os > con->optionStack) {
	cleanOSE(con->os--);
    }
    con->os->argb = PBM_FREE(con->os->argb);
    con->os->currAlias = NULL;
    con->os->nextCharArg = NULL;
    con->os->nextArg = NULL;
    con->os->next = 1;			/* skip argv[0] */

    con->numLeftovers = 0;
    con->nextLeftover = 0;
    con->restLeftover = 0;
    con->doExec = NULL;

    if (con->finalArgv != NULL)
    for (i = 0; i < con->finalArgvCount; i++) {
/*@-unqualifiedtrans@*/		/* FIX: typedef double indirection. */
	con->finalArgv[i] = _free(con->finalArgv[i]);
/*@=unqualifiedtrans@*/
    }

    con->finalArgvCount = 0;
    con->arg_strip = PBM_FREE(con->arg_strip);
/*@-nullstate@*/	/* FIX: con->finalArgv != NULL */
    return;
/*@=nullstate@*/
}

/* Only one of longName, shortName should be set, not both. */
static int handleExec(/*@special@*/ poptContext con,
		/*@null@*/ const char * longName, char shortName)
	/*@uses con->execs, con->numExecs, con->flags, con->doExec,
		con->finalArgv, con->finalArgvAlloced, con->finalArgvCount @*/
	/*@modifies con @*/
{
    poptItem item;
    int i;

    if (con->execs == NULL || con->numExecs <= 0) /* XXX can't happen */
	return 0;

    for (i = con->numExecs - 1; i >= 0; i--) {
	item = con->execs + i;
	if (longName && !(item->option.longName &&
			!strcmp(longName, item->option.longName)))
	    continue;
	else if (shortName != item->option.shortName)
	    continue;
	break;
    }
    if (i < 0) return 0;


    if (con->flags & POPT_CONTEXT_NO_EXEC)
	return 1;

    if (con->doExec == NULL) {
	con->doExec = con->execs + i;
	return 1;
    }

    /* We already have an exec to do; remember this option for next
       time 'round */
    if ((con->finalArgvCount + 1) >= (con->finalArgvAlloced)) {
	con->finalArgvAlloced += 10;
	con->finalArgv = realloc(con->finalArgv,
			sizeof(*con->finalArgv) * con->finalArgvAlloced);
    }

    i = con->finalArgvCount++;
    if (con->finalArgv != NULL)	/* XXX can't happen */
    {	char *s  = malloc((longName ? strlen(longName) : 0) + sizeof("--"));
	if (s != NULL) {	/* XXX can't happen */
	    con->finalArgv[i] = s;
	    *s++ = '-';
	    if (longName)
		s = stpcpy( stpcpy(s, "-"), longName);
	    else
		*s++ = shortName;
	    *s = '\0';
	} else
	    con->finalArgv[i] = NULL;
    }

    return 1;
}

/* Only one of longName, shortName may be set at a time */
static int handleAlias(/*@special@*/ poptContext con,
		/*@null@*/ const char * longName, size_t longNameLen,
		char shortName,
		/*@exposed@*/ /*@null@*/ const char * nextArg)
	/*@uses con->aliases, con->numAliases, con->optionStack, con->os,
		con->os->currAlias, con->os->currAlias->option.longName @*/
	/*@modifies con @*/
{
    poptItem item = con->os->currAlias;
    int rc;
    int i;

    if (item) {
	if (longName && item->option.longName != NULL
	 && longNameLen == strlen(item->option.longName)
	 && !strncmp(longName, item->option.longName, longNameLen))
	    return 0;
	else
	if (shortName && shortName == item->option.shortName)
	    return 0;
    }

    if (con->aliases == NULL || con->numAliases <= 0) /* XXX can't happen */
	return 0;

    for (i = con->numAliases - 1; i >= 0; i--) {
	item = con->aliases + i;
	if (longName) {
	    if (item->option.longName == NULL)
		continue;
	    if (longNameLen != strlen(item->option.longName))
		continue;
	    if (strncmp(longName, item->option.longName, longNameLen))
		continue;
	} else if (shortName != item->option.shortName)
	    continue;
	break;
    }
    if (i < 0) return 0;

    if ((con->os - con->optionStack + 1) == POPT_OPTION_DEPTH)
	return POPT_ERROR_OPTSTOODEEP;

    if (longName == NULL && nextArg != NULL && *nextArg != '\0')
	con->os->nextCharArg = nextArg;

    con->os++;
    con->os->next = 0;
    con->os->stuffed = 0;
    con->os->nextArg = NULL;
    con->os->nextCharArg = NULL;
    con->os->currAlias = con->aliases + i;
    {	const char ** av;
	int ac = con->os->currAlias->argc;
	/* Append --foo=bar arg to alias argv array (if present). */ 
	if (longName && nextArg != NULL && *nextArg != '\0') {
	    av = malloc((ac + 1 + 1) * sizeof(*av));
	    if (av != NULL) {	/* XXX won't happen. */
		for (i = 0; i < ac; i++) {
		    av[i] = con->os->currAlias->argv[i];
		}
		av[ac++] = nextArg;
		av[ac] = NULL;
	    } else	/* XXX revert to old popt behavior if malloc fails. */
		av = con->os->currAlias->argv;
	} else
	    av = con->os->currAlias->argv;
	rc = poptDupArgv(ac, av, &con->os->argc, &con->os->argv);
	if (av != NULL && av != con->os->currAlias->argv)
	    free(av);
    }
    con->os->argb = NULL;

    return (rc ? rc : 1);
}

/**
 * Return absolute path to executable by searching PATH.
 * @param argv0		name of executable
 * @return		(malloc'd) absolute path to executable (or NULL)
 */
static /*@null@*/
const char * findProgramPath(/*@null@*/ const char * argv0)
	/*@*/
{
    char *path = NULL, *s = NULL, *se;
    char *t = NULL;

    if (argv0 == NULL) return NULL;	/* XXX can't happen */

    /* If there is a / in argv[0], it has to be an absolute path. */
    /* XXX Hmmm, why not if (argv0[0] == '/') ... instead? */
    if (strchr(argv0, '/'))
	return xstrdup(argv0);

    if ((path = getenv("PATH")) == NULL || (path = xstrdup(path)) == NULL)
	return NULL;

    /* The return buffer in t is big enough for any path. */
    if ((t = malloc(strlen(path) + strlen(argv0) + sizeof("/"))) != NULL)
    for (s = path; s && *s; s = se) {

	/* Snip PATH element into [s,se). */
	if ((se = strchr(s, ':')))
	    *se++ = '\0';

	/* Append argv0 to PATH element. */
	(void) stpcpy(stpcpy(stpcpy(t, s), "/"), argv0);

	/* If file is executable, bingo! */
	if (!access(t, X_OK))
	    break;
    }

    /* If no executable was found in PATH, return NULL. */
    if (!(s && *s) && t != NULL) {
	free(t);
	t = NULL;
    }
/*@-modobserver -observertrans -usedef @*/
    if (path != NULL)
        free(path);
/*@=modobserver =observertrans =usedef @*/

    return t;
}

static int execCommand(poptContext con)
	/*@globals internalState @*/
	/*@modifies internalState @*/
{
    poptItem item = con->doExec;
    poptArgv argv = NULL;
    int argc = 0;
    int rc;
    int ec = POPT_ERROR_ERRNO;

    if (item == NULL) /*XXX can't happen*/
	return POPT_ERROR_NOARG;

    if (item->argv == NULL || item->argc < 1 ||
	(!con->execAbsolute && strchr(item->argv[0], '/')))
	    return POPT_ERROR_NOARG;

    argv = malloc(sizeof(*argv) *
			(6 + item->argc + con->numLeftovers + con->finalArgvCount));
    if (argv == NULL) return POPT_ERROR_MALLOC;

    if (!strchr(item->argv[0], '/') && con->execPath != NULL) {
	char *s = malloc(strlen(con->execPath) + strlen(item->argv[0]) + sizeof("/"));
	if (s)
	    (void)stpcpy(stpcpy(stpcpy(s, con->execPath), "/"), item->argv[0]);

	argv[argc] = s;
    } else
	argv[argc] = findProgramPath(item->argv[0]);
    if (argv[argc++] == NULL) {
	ec = POPT_ERROR_NOARG;
	goto exit;
    }

    if (item->argc > 1) {
	memcpy(argv + argc, item->argv + 1, sizeof(*argv) * (item->argc - 1));
	argc += (item->argc - 1);
    }

    if (con->finalArgv != NULL && con->finalArgvCount > 0) {
	memcpy(argv + argc, con->finalArgv,
		sizeof(*argv) * con->finalArgvCount);
	argc += con->finalArgvCount;
    }

    if (con->leftovers != NULL && con->numLeftovers > 0) {
	memcpy(argv + argc, con->leftovers, sizeof(*argv) * con->numLeftovers);
	argc += con->numLeftovers;
    }

    argv[argc] = NULL;

#if defined(hpux) || defined(__hpux)
    rc = setresgid(getgid(), getgid(),-1);
    if (rc) goto exit;
    rc = setresuid(getuid(), getuid(),-1);
    if (rc) goto exit;
#else
/*
 * XXX " ... on BSD systems setuid() should be preferred over setreuid()"
 * XXX 	sez' Timur Bakeyev <mc@bat.ru>
 * XXX	from Norbert Warmuth <nwarmuth@privat.circular.de>
 */
#if defined(HAVE_SETUID)
    rc = setgid(getgid());
    if (rc) goto exit;
    rc = setuid(getuid());
    if (rc) goto exit;
#elif defined (HAVE_SETREUID)
    rc = setregid(getgid(), getgid());
    if (rc) goto exit;
    rc = setreuid(getuid(), getuid());
    if (rc) goto exit;
#else
    ; /* Can't drop privileges */
#endif
#endif

#ifdef	MYDEBUG
if (_popt_debug)
    {	poptArgv avp;
	fprintf(stderr, "==> execvp(%s) argv[%d]:", argv[0], argc);
	for (avp = argv; *avp; avp++)
	    fprintf(stderr, " '%s'", *avp);
	fprintf(stderr, "\n");
    }
#endif

/*@-nullstate@*/
    rc = execvp(argv[0], (char *const *)argv);
/*@=nullstate@*/

exit:
    if (argv) {
        if (argv[0])
            free((void *)argv[0]);
        free(argv);
    }
    return ec;
}

/*@observer@*/ /*@null@*/ static const struct poptOption *
findOption(const struct poptOption * opt,
		/*@null@*/ const char * longName, size_t longNameLen,
		char shortName,
		/*@null@*/ /*@out@*/ poptCallbackType * callback,
		/*@null@*/ /*@out@*/ const void ** callbackData,
		int singleDash)
	/*@modifies *callback, *callbackData */
{
    const struct poptOption * cb = NULL;
    poptArg cbarg = { .ptr = NULL };

    /* This happens when a single - is given */
    if (singleDash && !shortName && (longName && *longName == '\0'))
	shortName = '-';

    for (; opt->longName || opt->shortName || opt->arg; opt++) {
	poptArg arg = { .ptr = opt->arg };

	if (poptArgType(opt) == POPT_ARG_INCLUDE_TABLE) {
	    const struct poptOption * opt2;

	    poptSubstituteHelpI18N(arg.opt);	/* XXX side effects */
	    /* Recurse on included sub-tables. */
	    if (arg.ptr == NULL) continue;	/* XXX program error */
	    opt2 = findOption(arg.opt, longName, longNameLen, shortName, callback,
			      callbackData, singleDash);
	    if (opt2 == NULL) continue;
	    /* Sub-table data will be inheirited if no data yet. */
	    if (!(callback && *callback)) return opt2;
	    if (!(callbackData && *callbackData == NULL)) return opt2;
/*@-observertrans -dependenttrans @*/
	    *callbackData = opt->descrip;
/*@=observertrans =dependenttrans @*/
	    return opt2;
	} else if (poptArgType(opt) == POPT_ARG_CALLBACK) {
	    cb = opt;
	    cbarg.ptr = opt->arg;
	} else if (longName != NULL && opt->longName != NULL &&
		   (!singleDash || F_ISSET(opt, ONEDASH)) &&
		   (!strncmp(longName, opt->longName, longNameLen) && strlen(opt->longName) == longNameLen))
	{
	    break;
	} else if (shortName && shortName == opt->shortName) {
	    break;
	}
    }

    if (opt->longName == NULL && !opt->shortName)
	return NULL;

/*@-modobserver -mods @*/
    if (callback)
	*callback = (cb ? cbarg.cb : NULL);
    if (callbackData)
/*@-observertrans@*/	/* FIX: typedef double indirection. */
	*callbackData = (cb && !CBF_ISSET(cb, INC_DATA) ? cb->descrip : NULL);
/*@=observertrans@*/
/*@=modobserver =mods @*/

    return opt;
}

static const char * findNextArg(/*@special@*/ poptContext con,
		unsigned argx, int delete_arg)
	/*@uses con->optionStack, con->os,
		con->os->next, con->os->argb, con->os->argc, con->os->argv @*/
	/*@modifies con @*/
{
    struct optionStackEntry * os = con->os;
    const char * arg;

    do {
	int i;
	arg = NULL;
	while (os->next == os->argc && os > con->optionStack) os--;
	if (os->next == os->argc && os == con->optionStack) break;
	if (os->argv != NULL)
	for (i = os->next; i < os->argc; i++) {
/*@-sizeoftype@*/
	    if (os->argb && PBM_ISSET(i, os->argb))
		/*@innercontinue@*/ continue;
	    if (*os->argv[i] == '-')
		/*@innercontinue@*/ continue;
	    if (--argx > 0)
		/*@innercontinue@*/ continue;
	    arg = os->argv[i];
	    if (delete_arg) {
		if (os->argb == NULL) os->argb = PBM_ALLOC(os->argc);
		if (os->argb != NULL)	/* XXX can't happen */
		    PBM_SET(i, os->argb);
	    }
	    /*@innerbreak@*/ break;
/*@=sizeoftype@*/
	}
	if (os > con->optionStack) os--;
    } while (arg == NULL);
    return arg;
}

static /*@only@*/ /*@null@*/ const char *
expandNextArg(/*@special@*/ poptContext con, const char * s)
	/*@uses con->optionStack, con->os,
		con->os->next, con->os->argb, con->os->argc, con->os->argv @*/
	/*@modifies con @*/
{
    const char * a = NULL;
    char *t, *te;
    size_t tn = strlen(s) + 1;
    char c;

    te = t = malloc(tn);
    if (t == NULL) return NULL;		/* XXX can't happen */
    *t = '\0';
    while ((c = *s++) != '\0') {
	switch (c) {
#if 0	/* XXX can't do this */
	case '\\':	/* escape */
	    c = *s++;
	    /*@switchbreak@*/ break;
#endif
	case '!':
	    if (!(s[0] == '#' && s[1] == ':' && s[2] == '+'))
		/*@switchbreak@*/ break;
	    /* XXX Make sure that findNextArg deletes only next arg. */
	    if (a == NULL) {
		if ((a = findNextArg(con, 1U, 1)) == NULL)
		    /*@switchbreak@*/ break;
	    }
	    s += 3;

	    tn += strlen(a);
	    {   size_t pos = (size_t) (te - t);
		t = realloc(t, tn);
		te = stpcpy(t + pos, a);
	    }
	    continue;
	    /*@notreached@*/ /*@switchbreak@*/ break;
	default:
	    /*@switchbreak@*/ break;
	}
	*te++ = c;
    }
    *te++ = '\0';
    /* If the new string is longer than needed, shorten. */
    if ((t + tn) > te) {
/*@-usereleased@*/	/* XXX splint can't follow the pointers. */
	if ((te = realloc(t, (size_t)(te - t))) == NULL)
	    free(t);
	t = te;
/*@=usereleased@*/
    }
    return t;
}

static void poptStripArg(/*@special@*/ poptContext con, int which)
	/*@uses con->optionStack @*/
	/*@defines con->arg_strip @*/
	/*@modifies con @*/
{
/*@-compdef -sizeoftype -usedef @*/
    if (con->arg_strip == NULL)
	con->arg_strip = PBM_ALLOC(con->optionStack[0].argc);
    if (con->arg_strip != NULL)		/* XXX can't happen */
    PBM_SET(which, con->arg_strip);
    return;
/*@=compdef =sizeoftype =usedef @*/
}

int poptSaveString(const char *** argvp,
		/*@unused@*/ UNUSED(unsigned int argInfo), const char * val)
{
    int argc = 0;

    if (argvp == NULL)
	return -1;

    /* XXX likely needs an upper bound on argc. */
    if (*argvp != NULL)
    while ((*argvp)[argc] != NULL)
	argc++;
 
/*@-unqualifiedtrans -nullstate@*/	/* XXX no annotation for (*argvp) */
    if ((*argvp = xrealloc(*argvp, (argc + 1 + 1) * sizeof(**argvp))) != NULL) {
	(*argvp)[argc++] = xstrdup(val);
	(*argvp)[argc  ] = NULL;
    }
    return 0;
/*@=unqualifiedtrans =nullstate@*/
}

/*@unchecked@*/
static unsigned int seed = 0;

int poptSaveLongLong(long long * arg, unsigned int argInfo, long long aLongLong)
{
    if (arg == NULL
#ifdef	NOTYET
    /* XXX Check alignment, may fail on funky platforms. */
     || (((unsigned long long)arg) & (sizeof(*arg)-1))
#endif
    )
	return POPT_ERROR_NULLARG;

    if (aLongLong != 0 && LF_ISSET(RANDOM)) {
	if (!seed) {
	    srandom((unsigned)getpid());
	    srandom((unsigned)random());
	}
	aLongLong = (long long)(random() % (aLongLong > 0 ? aLongLong : -aLongLong));
	aLongLong++;
    }
    if (LF_ISSET(NOT))
	aLongLong = ~aLongLong;
    switch (LF_ISSET(LOGICALOPS)) {
    case 0:
	*arg = aLongLong;
	break;
    case POPT_ARGFLAG_OR:
	*(unsigned long long *)arg |= (unsigned long long)aLongLong;
	break;
    case POPT_ARGFLAG_AND:
	*(unsigned long long *)arg &= (unsigned long long)aLongLong;
	break;
    case POPT_ARGFLAG_XOR:
	*(unsigned long long *)arg ^= (unsigned long long)aLongLong;
	break;
    default:
	return POPT_ERROR_BADOPERATION;
	/*@notreached@*/ break;
    }
    return 0;
}

int poptSaveLong(long * arg, unsigned int argInfo, long aLong)
{
    /* XXX Check alignment, may fail on funky platforms. */
    if (arg == NULL || (((unsigned long)arg) & (sizeof(*arg)-1)))
	return POPT_ERROR_NULLARG;

    if (aLong != 0 && LF_ISSET(RANDOM)) {
	if (!seed) {
	    srandom((unsigned)getpid());
	    srandom((unsigned)random());
	}
	aLong = random() % (aLong > 0 ? aLong : -aLong);
	aLong++;
    }
    if (LF_ISSET(NOT))
	aLong = ~aLong;
    switch (LF_ISSET(LOGICALOPS)) {
    case 0:
	*arg = aLong;
	break;
    case POPT_ARGFLAG_OR:
	*(unsigned long *)arg |= (unsigned long)aLong;
	break;
    case POPT_ARGFLAG_AND:
	*(unsigned long *)arg &= (unsigned long)aLong;
	break;
    case POPT_ARGFLAG_XOR:
	*(unsigned long *)arg ^= (unsigned long)aLong;
	break;
    default:
	return POPT_ERROR_BADOPERATION;
	/*@notreached@*/ break;
    }
    return 0;
}

int poptSaveInt(/*@null@*/ int * arg, unsigned int argInfo, long aLong)
{
    /* XXX Check alignment, may fail on funky platforms. */
    if (arg == NULL || (((unsigned long)arg) & (sizeof(*arg)-1)))
	return POPT_ERROR_NULLARG;

    if (aLong != 0 && LF_ISSET(RANDOM)) {
	if (!seed) {
	    srandom((unsigned)getpid());
	    srandom((unsigned)random());
	}
	aLong = random() % (aLong > 0 ? aLong : -aLong);
	aLong++;
    }
    if (LF_ISSET(NOT))
	aLong = ~aLong;
    switch (LF_ISSET(LOGICALOPS)) {
    case 0:
	*arg = (int) aLong;
	break;
    case POPT_ARGFLAG_OR:
	*(unsigned int *)arg |= (unsigned int) aLong;
	break;
    case POPT_ARGFLAG_AND:
	*(unsigned int *)arg &= (unsigned int) aLong;
	break;
    case POPT_ARGFLAG_XOR:
	*(unsigned int *)arg ^= (unsigned int) aLong;
	break;
    default:
	return POPT_ERROR_BADOPERATION;
	/*@notreached@*/ break;
    }
    return 0;
}

/* returns 'val' element, -1 on last item, POPT_ERROR_* on error */
int poptGetNextOpt(poptContext con)
{
    const struct poptOption * opt = NULL;
    int done = 0;

    if (con == NULL)
	return -1;
    while (!done) {
	const char * origOptString = NULL;
	poptCallbackType cb = NULL;
	const void * cbData = NULL;
	const char * longArg = NULL;
	int canstrip = 0;
	int shorty = 0;

	while (!con->os->nextCharArg && con->os->next == con->os->argc
		&& con->os > con->optionStack) {
	    cleanOSE(con->os--);
	}
	if (!con->os->nextCharArg && con->os->next == con->os->argc) {
	    invokeCallbacksPOST(con, con->options);

	    if (con->maincall) {
		/*@-noeffectuncon @*/
		(void) (*con->maincall) (con->finalArgvCount, con->finalArgv);
		/*@=noeffectuncon @*/
		return -1;
	    }

	    if (con->doExec) return execCommand(con);
	    return -1;
	}

	/* Process next long option */
	if (!con->os->nextCharArg) {
	    const char * optString;
            size_t optStringLen;
	    int thisopt;

/*@-sizeoftype@*/
	    if (con->os->argb && PBM_ISSET(con->os->next, con->os->argb)) {
		con->os->next++;
		continue;
	    }
/*@=sizeoftype@*/
	    thisopt = con->os->next;
	    if (con->os->argv != NULL)	/* XXX can't happen */
	    origOptString = con->os->argv[con->os->next++];

	    if (origOptString == NULL)	/* XXX can't happen */
		return POPT_ERROR_BADOPT;

	    if (con->restLeftover || *origOptString != '-' ||
		(*origOptString == '-' && origOptString[1] == '\0'))
	    {
		if (con->flags & POPT_CONTEXT_POSIXMEHARDER)
		    con->restLeftover = 1;
		if (con->flags & POPT_CONTEXT_ARG_OPTS) {
		    con->os->nextArg = xstrdup(origOptString);
		    return 0;
		}
		if (con->leftovers != NULL)	/* XXX can't happen */
		    con->leftovers[con->numLeftovers++] = origOptString;
		continue;
	    }

	    /* Make a copy we can hack at */
	    optString = origOptString;

	    if (optString[0] == '\0')
		return POPT_ERROR_BADOPT;

	    if (optString[1] == '-' && !optString[2]) {
		con->restLeftover = 1;
		continue;
	    } else {
		const char *oe;
		int singleDash;

		optString++;
		if (*optString == '-')
		    singleDash = 0, optString++;
		else
		    singleDash = 1;

		/* Check for "--long=arg" option. */
		for (oe = optString; *oe && *oe != '='; oe++)
		    {};
		optStringLen = (size_t)(oe - optString);
		if (*oe == '=')
		    longArg = oe + 1;

		/* XXX aliases with arg substitution need "--alias=arg" */
		if (handleAlias(con, optString, optStringLen, '\0', longArg)) {
		    longArg = NULL;
		    continue;
		}

		if (handleExec(con, optString, '\0'))
		    continue;

		opt = findOption(con->options, optString, optStringLen, '\0', &cb, &cbData,
				 singleDash);
		if (!opt && !singleDash)
		    return POPT_ERROR_BADOPT;
	    }

	    if (!opt) {
		con->os->nextCharArg = origOptString + 1;
		longArg = NULL;
	    } else {
		if (con->os == con->optionStack && F_ISSET(opt, STRIP))
		{
		    canstrip = 1;
		    poptStripArg(con, thisopt);
		}
		shorty = 0;
	    }
	}

	/* Process next short option */
	if (con->os->nextCharArg) {
	    origOptString = con->os->nextCharArg;

	    con->os->nextCharArg = NULL;

	    if (handleAlias(con, NULL, 0, *origOptString, origOptString + 1))
		continue;

	    if (handleExec(con, NULL, *origOptString)) {
		/* Restore rest of short options for further processing */
		origOptString++;
		if (*origOptString != '\0')
		    con->os->nextCharArg = origOptString;
		continue;
	    }

	    opt = findOption(con->options, NULL, 0, *origOptString, &cb,
			     &cbData, 0);
	    if (!opt)
		return POPT_ERROR_BADOPT;
	    shorty = 1;

	    origOptString++;
	    if (*origOptString != '\0')
		con->os->nextCharArg = origOptString + (int)(*origOptString == '=');
	}

	if (opt == NULL) return POPT_ERROR_BADOPT;	/* XXX can't happen */
	if (opt->arg && poptArgType(opt) == POPT_ARG_NONE) {
	    if (poptSaveInt((int *)opt->arg, opt->argInfo, 1L))
		return POPT_ERROR_BADOPERATION;
	} else if (poptArgType(opt) == POPT_ARG_VAL) {
	    if (opt->arg) {
		if (poptSaveInt((int *)opt->arg, opt->argInfo, (long)opt->val))
		    return POPT_ERROR_BADOPERATION;
	    }
	} else if (poptArgType(opt) != POPT_ARG_NONE) {
	    con->os->nextArg = _free(con->os->nextArg);
	    if (longArg) {
		longArg = expandNextArg(con, longArg);
		con->os->nextArg = (char *) longArg;
	    } else if (con->os->nextCharArg) {
		longArg = expandNextArg(con, con->os->nextCharArg);
		con->os->nextArg = (char *) longArg;
		con->os->nextCharArg = NULL;
	    } else {
		while (con->os->next == con->os->argc &&
			con->os > con->optionStack)
		{
		    cleanOSE(con->os--);
		}
		if (con->os->next == con->os->argc) {
		    if (!F_ISSET(opt, OPTIONAL))
			return POPT_ERROR_NOARG;
		    con->os->nextArg = NULL;
		} else {

		    /*
		     * Make sure this isn't part of a short arg or the
		     * result of an alias expansion.
		     */
		    if (con->os == con->optionStack
		     && F_ISSET(opt, STRIP) && canstrip)
		    {
			poptStripArg(con, con->os->next);
		    }
		
		    if (con->os->argv != NULL) {	/* XXX can't happen */
			if (F_ISSET(opt, OPTIONAL) &&
			    con->os->argv[con->os->next][0] == '-') {
			    con->os->nextArg = NULL;
			} else {
			    /* XXX watchout: subtle side-effects live here. */
			    longArg = con->os->argv[con->os->next++];
			    longArg = expandNextArg(con, longArg);
			    con->os->nextArg = (char *) longArg;
			}
		    }
		}
	    }
	    longArg = NULL;

	    if (opt->arg) {
		poptArg arg = { .ptr = opt->arg };
		switch (poptArgType(opt)) {
		case POPT_ARG_ARGV:
		    /* XXX memory leak, application is responsible for free. */
		    if (con->os->nextArg == NULL)
			return POPT_ERROR_NULLARG;	/* XXX better return? */
		    if (poptSaveString(arg.ptr, opt->argInfo, con->os->nextArg))
			return POPT_ERROR_BADOPERATION;
		    /*@switchbreak@*/ break;
		case POPT_ARG_STRING:
		    /* XXX memory leak, application is responsible for free. */
		    arg.argv[0] = (con->os->nextArg)
			? xstrdup(con->os->nextArg) : NULL;
		    /*@switchbreak@*/ break;

		case POPT_ARG_INT:
		case POPT_ARG_LONG:
		case POPT_ARG_LONGLONG:
		{   long long aNUM = 0;
		    char *end = NULL;

		    if (con->os->nextArg) {
			aNUM = strtoll(con->os->nextArg, &end, 0);
			if (!(end && *end == '\0'))
			    return POPT_ERROR_BADNUMBER;
		    }

/* XXX let's not demand C99 compiler flags for <limits.h> quite yet. */
#if !defined(LLONG_MAX)
#   define LLONG_MAX    9223372036854775807LL
#   define LLONG_MIN    (-LLONG_MAX - 1LL)
#endif

		    if (poptArgType(opt) == POPT_ARG_LONGLONG) {
			if (aNUM == LLONG_MAX || aNUM == LLONG_MIN)
			    return POPT_ERROR_OVERFLOW;
			if (poptSaveLongLong(arg.longlongp, opt->argInfo, aNUM))
			    return POPT_ERROR_BADOPERATION;
		    } else
		    if (poptArgType(opt) == POPT_ARG_LONG) {
			if (aNUM > (long long)LONG_MAX || aNUM < (long long)LONG_MIN)
			    return POPT_ERROR_OVERFLOW;
			if (poptSaveLong(arg.longp, opt->argInfo, (long)aNUM))
			    return POPT_ERROR_BADOPERATION;
		    } else
		    if (poptArgType(opt) == POPT_ARG_INT) {
			if (aNUM > (long long)INT_MAX || aNUM < (long long)INT_MIN)
			    return POPT_ERROR_OVERFLOW;
			if (poptSaveInt(arg.intp, opt->argInfo, (long)aNUM))
			    return POPT_ERROR_BADOPERATION;
		    } else
			return POPT_ERROR_BADOPERATION;
		}   /*@switchbreak@*/ break;

		case POPT_ARG_FLOAT:
		case POPT_ARG_DOUBLE:
		{   double aDouble = 0.0;
		    char *end;

		    if (con->os->nextArg) {
/*@-mods@*/
			int saveerrno = errno;
			errno = 0;
			aDouble = strtod(con->os->nextArg, &end);
			if (errno == ERANGE)
			    return POPT_ERROR_OVERFLOW;
			errno = saveerrno;
/*@=mods@*/
			if (*end != '\0')
			    return POPT_ERROR_BADNUMBER;
		    }

		    if (poptArgType(opt) == POPT_ARG_DOUBLE) {
			arg.doublep[0] = aDouble;
		    } else {
#if !defined(DBL_EPSILON) && !defined(__LCLINT__)
#define DBL_EPSILON 2.2204460492503131e-16
#endif
#define POPT_ABS(a)	((((a) - 0.0) < DBL_EPSILON) ? -(a) : (a))
			if ((POPT_ABS(aDouble) - FLT_MAX) > DBL_EPSILON)
			    return POPT_ERROR_OVERFLOW;
			if ((FLT_MIN - POPT_ABS(aDouble)) > DBL_EPSILON)
			    return POPT_ERROR_OVERFLOW;
			arg.floatp[0] = (float) aDouble;
		    }
		}   /*@switchbreak@*/ break;
		case POPT_ARG_MAINCALL:
/*@-type@*/
		    con->maincall = opt->arg;
/*@=type@*/
		    /*@switchbreak@*/ break;
		default:
		    fprintf(stdout,
			POPT_("option type (%u) not implemented in popt\n"),
			poptArgType(opt));
		    exit(EXIT_FAILURE);
		    /*@notreached@*/ /*@switchbreak@*/ break;
		}
	    }
	}

	if (cb)
	    invokeCallbacksOPTION(con, con->options, opt, cbData, shorty);
	else if (opt->val && (poptArgType(opt) != POPT_ARG_VAL))
	    done = 1;

	if ((con->finalArgvCount + 2) >= (con->finalArgvAlloced)) {
	    con->finalArgvAlloced += 10;
	    con->finalArgv = realloc(con->finalArgv,
			    sizeof(*con->finalArgv) * con->finalArgvAlloced);
	}

	if (con->finalArgv != NULL)
	{   char *s = malloc((opt->longName ? strlen(opt->longName) : 0) + sizeof("--"));
	    if (s != NULL) {	/* XXX can't happen */
		con->finalArgv[con->finalArgvCount++] = s;
		*s++ = '-';
		if (opt->longName) {
		    if (!F_ISSET(opt, ONEDASH))
			*s++ = '-';
		    s = stpcpy(s, opt->longName);
		} else {
		    *s++ = opt->shortName;
		    *s = '\0';
		}
	    } else
		con->finalArgv[con->finalArgvCount++] = NULL;
	}

	if (opt->arg && poptArgType(opt) == POPT_ARG_NONE)
	    /*@-ifempty@*/ ; /*@=ifempty@*/
	else if (poptArgType(opt) == POPT_ARG_VAL)
	    /*@-ifempty@*/ ; /*@=ifempty@*/
	else if (poptArgType(opt) != POPT_ARG_NONE) {
	    if (con->finalArgv != NULL && con->os->nextArg != NULL)
	        con->finalArgv[con->finalArgvCount++] =
			xstrdup(con->os->nextArg);
	}
    }

    return (opt ? opt->val : -1);	/* XXX can't happen */
}

char * poptGetOptArg(poptContext con)
{
    char * ret = NULL;
    if (con) {
	ret = con->os->nextArg;
	con->os->nextArg = NULL;
    }
    return ret;
}

const char * poptGetArg(poptContext con)
{
    const char * ret = NULL;
    if (con && con->leftovers != NULL && con->nextLeftover < con->numLeftovers)
	ret = con->leftovers[con->nextLeftover++];
    return ret;
}

const char * poptPeekArg(poptContext con)
{
    const char * ret = NULL;
    if (con && con->leftovers != NULL && con->nextLeftover < con->numLeftovers)
	ret = con->leftovers[con->nextLeftover];
    return ret;
}

const char ** poptGetArgs(poptContext con)
{
    if (con == NULL ||
	con->leftovers == NULL || con->numLeftovers == con->nextLeftover)
	return NULL;

    /* some apps like [like RPM ;-) ] need this NULL terminated */
    con->leftovers[con->numLeftovers] = NULL;

/*@-nullret -nullstate @*/	/* FIX: typedef double indirection. */
    return (con->leftovers + con->nextLeftover);
/*@=nullret =nullstate @*/
}

static /*@null@*/
poptItem poptFreeItems(/*@only@*/ /*@null@*/ poptItem items, int nitems)
	/*@modifies items @*/
{
    if (items != NULL) {
	poptItem item = items;
	while (--nitems >= 0) {
/*@-modobserver -observertrans -dependenttrans@*/
	    item->option.longName = _free(item->option.longName);
	    item->option.descrip = _free(item->option.descrip);
	    item->option.argDescrip = _free(item->option.argDescrip);
/*@=modobserver =observertrans =dependenttrans@*/
	    item->argv = _free(item->argv);
	    item++;
	}
	items = _free(items);
    }
    return NULL;
}

poptContext poptFreeContext(poptContext con)
{
    if (con == NULL) return con;
    poptResetContext(con);
    con->os->argb = _free(con->os->argb);

    con->aliases = poptFreeItems(con->aliases, con->numAliases);
    con->numAliases = 0;

    con->execs = poptFreeItems(con->execs, con->numExecs);
    con->numExecs = 0;

    con->leftovers = _free(con->leftovers);
    con->finalArgv = _free(con->finalArgv);
    con->appName = _free(con->appName);
    con->otherHelp = _free(con->otherHelp);
    con->execPath = _free(con->execPath);
    con->arg_strip = PBM_FREE(con->arg_strip);
    
    con = _free(con);
    return con;
}

int poptAddAlias(poptContext con, struct poptAlias alias,
		/*@unused@*/ UNUSED(int flags))
{
    struct poptItem_s item_buf;
    poptItem item = &item_buf;
    memset(item, 0, sizeof(*item));
    item->option.longName = alias.longName;
    item->option.shortName = alias.shortName;
    item->option.argInfo = POPT_ARGFLAG_DOC_HIDDEN;
    item->option.arg = 0;
    item->option.val = 0;
    item->option.descrip = NULL;
    item->option.argDescrip = NULL;
    item->argc = alias.argc;
    item->argv = alias.argv;
    return poptAddItem(con, item, 0);
}

int poptAddItem(poptContext con, poptItem newItem, int flags)
{
    poptItem * items, item;
    int * nitems;

    switch (flags) {
    case 1:
	items = &con->execs;
	nitems = &con->numExecs;
	break;
    case 0:
	items = &con->aliases;
	nitems = &con->numAliases;
	break;
    default:
	return 1;
	/*@notreached@*/ break;
    }

    *items = realloc((*items), ((*nitems) + 1) * sizeof(**items));
    if ((*items) == NULL)
	return 1;

    item = (*items) + (*nitems);

    item->option.longName =
	(newItem->option.longName ? xstrdup(newItem->option.longName) : NULL);
    item->option.shortName = newItem->option.shortName;
    item->option.argInfo = newItem->option.argInfo;
    item->option.arg = newItem->option.arg;
    item->option.val = newItem->option.val;
    item->option.descrip =
	(newItem->option.descrip ? xstrdup(newItem->option.descrip) : NULL);
    item->option.argDescrip =
       (newItem->option.argDescrip ? xstrdup(newItem->option.argDescrip) : NULL);
    item->argc = newItem->argc;
    item->argv = newItem->argv;

    (*nitems)++;

    return 0;
}

const char * poptBadOption(poptContext con, unsigned int flags)
{
    struct optionStackEntry * os = NULL;

    if (con != NULL)
	os = (flags & POPT_BADOPTION_NOALIAS) ? con->optionStack : con->os;

    return (os != NULL && os->argv != NULL ? os->argv[os->next - 1] : NULL);
}

const char * poptStrerror(const int error)
{
    switch (error) {
      case POPT_ERROR_NOARG:
	return POPT_("missing argument");
      case POPT_ERROR_BADOPT:
	return POPT_("unknown option");
      case POPT_ERROR_BADOPERATION:
	return POPT_("mutually exclusive logical operations requested");
      case POPT_ERROR_NULLARG:
	return POPT_("opt->arg should not be NULL");
      case POPT_ERROR_OPTSTOODEEP:
	return POPT_("aliases nested too deeply");
      case POPT_ERROR_BADQUOTE:
	return POPT_("error in parameter quoting");
      case POPT_ERROR_BADNUMBER:
	return POPT_("invalid numeric value");
      case POPT_ERROR_OVERFLOW:
	return POPT_("number too large or too small");
      case POPT_ERROR_MALLOC:
	return POPT_("memory allocation failed");
      case POPT_ERROR_ERRNO:
	return strerror(errno);
      default:
	return POPT_("unknown error");
    }
}

int poptStuffArgs(poptContext con, const char ** argv)
{
    int argc;
    int rc;

    if ((con->os - con->optionStack) == POPT_OPTION_DEPTH)
	return POPT_ERROR_OPTSTOODEEP;

    for (argc = 0; argv[argc]; argc++)
	{};

    con->os++;
    con->os->next = 0;
    con->os->nextArg = NULL;
    con->os->nextCharArg = NULL;
    con->os->currAlias = NULL;
    rc = poptDupArgv(argc, argv, &con->os->argc, &con->os->argv);
    con->os->argb = NULL;
    con->os->stuffed = 1;

    return rc;
}

const char * poptGetInvocationName(poptContext con)
{
    return (con->os->argv ? con->os->argv[0] : "");
}

int poptStrippedArgv(poptContext con, int argc, char ** argv)
{
    int numargs = argc;
    int j = 1;
    int i;
    
/*@-sizeoftype@*/
    if (con->arg_strip)
    for (i = 1; i < argc; i++) {
	if (PBM_ISSET(i, con->arg_strip))
	    numargs--;
    }
    
    for (i = 1; i < argc; i++) {
	if (con->arg_strip && PBM_ISSET(i, con->arg_strip))
	    continue;
	argv[j] = (j < numargs) ? argv[i] : NULL;
	j++;
    }
/*@=sizeoftype@*/
    
    return numargs;
}
