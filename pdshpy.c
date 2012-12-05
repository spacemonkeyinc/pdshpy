/* Copyright (c) 2012 by Space Monkey, Inc.
 *
 * Based on code from pdsh, Copyright (C) 2001-2006 The Regents of the
 * University of California.
 *
 *  Pdshpy is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  Pdshpy is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Pdshpy; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <Python.h>
#include "src/common/hostlist.h"
#include "src/common/err.h"
#include "src/common/xmalloc.h"
#include "src/pdsh/mod.h"
#include "src/pdsh/rcmd.h"

int pdsh_module_priority = 110;

static int pdshpy_init(void);
static int pdshpy_process_opt(opt_t *, int, char *);
static hostlist_t pdshpy_wcoll(opt_t *pdsh_opts);
static int pdshpy_postop(opt_t *);
static int pdshpy_fini(void);
static PyObject *register_option(PyObject *self, PyObject *args);
static PyObject *pdshpy_rcmd_register_defaults(PyObject *self, PyObject *args);

/* the default name of the Python module to use for the pdsh functionality */
#define PDSHPY_PYTHON_MODULE "pdshpy_module"

/* the name of the Python module which complements this C module */
#define PDSHPY_UTIL_MODULE "pdshpy.util"

/* the name of the internal module which will give Python access to pdsh
 * functionality (not an actual Python file) */
#define PDSHPY_PYTHON_INTERNAL "_pdshpy_internal"

/* use the environment variable with this name to specify a particular Python
 * module instead of PDSHPY_PYTHON_MODULE */
#define PDSHPY_ENVIRON_MODULENAME "PDSHPY_MODULE"

/* set the environment variable with this name to a positive number to enable
 * debug statements */
#define PDSHPY_ENVIRON_DEBUG "PDSHPY_DEBUG"

/* this will be output in front of output lines */
#define PDSHPY_LOG_PREFIX "pdshpy"

static int debuglevel = 0;
static int options_registered = 0;

#define DBG(tmpl, args...) \
    ({ if (debuglevel > 0) \
            fprintf(stderr, PDSHPY_LOG_PREFIX ": " tmpl "\n", ## args); })

#define ERR(tmpl, args...) \
    ({ fprintf(stderr, PDSHPY_LOG_PREFIX ": " tmpl "\n", ## args); })

#define PYERR(tmpl, args...) \
    ({ ERR(tmpl, ## args); PyErr_Print(); })

static PyObject *pymodule = NULL;
static PyObject *pymodule_util = NULL;
static PyObject *pymodule_internal = NULL;
static PyObject *pymodule_data = NULL;

struct pdsh_module_operations pdshpy_module_ops = {
    (ModInitF)       pdshpy_init,
    (ModExitF)       pdshpy_fini,
    (ModReadWcollF)  pdshpy_wcoll,
    (ModPostOpF)     pdshpy_postop,
};

struct pdsh_rcmd_operations pdshpy_rcmd_ops = {
    (RcmdInitF)  NULL,
    (RcmdSigF)   NULL,
    (RcmdF)      NULL,
};

struct pdsh_module_option null_option = PDSH_OPT_TABLE_END;

struct pdsh_module pdsh_module_info = {
    "misc",
    "pdshpy",
    "paul cannon <paul@spacemonkey.com>",
    "Allow use of Python modules with pdsh",
    DSH | PCP,

    &pdshpy_module_ops,
    &pdshpy_rcmd_ops,
    &null_option,
};

static PyMethodDef pdshpy_methods[] = {
    {"_register_option", register_option, METH_VARARGS,
     "Register a pdsh option to be recognized by this module."},
    {"_rcmd_register_defaults", pdshpy_rcmd_register_defaults, METH_VARARGS,
     "Register default rcmd parameters for given hosts"},
    {NULL, NULL, 0, NULL}
};

static PyObject *
register_option(PyObject *self, PyObject *args)
{
    const char *opt_letter_str = NULL;
    const char *argmeta = NULL;
    const char *desc = NULL;
    int personality = 0;
    struct pdsh_module_option *new_opt_table = NULL;

    if (!PyArg_ParseTuple(args, "sziz",
                          &opt_letter_str, &argmeta, &personality, &desc))
        return NULL;

    if (opt_letter_str[0] == '\0')
    {
        PyErr_SetString(PyExc_ValueError,
                        "Option letter must not be the empty string");
        return NULL;
    }
    if (opt_letter_str[1] != '\0')
    {
        PyErr_SetString(PyExc_ValueError,
                        "Option letter string must be exactly one character");
        return NULL;
    }

    /* It looks pretty safe to mess with this module option table after
     * module initialization with the current pdsh code, but I don't think
     * it's meant to be a supported thing to do.
     */
    if (options_registered == 0)
    {
        /* the old table was allocated statically. tell realloc not to free */
        pdsh_module_info.opt_table = NULL;
    }
    options_registered++;

    new_opt_table = realloc(
            pdsh_module_info.opt_table,
            (options_registered + 1) * sizeof(struct pdsh_module_option));
    pdsh_module_info.opt_table = new_opt_table;
    new_opt_table = &new_opt_table[options_registered - 1];

    new_opt_table->opt = opt_letter_str[0];
    new_opt_table->arginfo = Strdup(argmeta);
    new_opt_table->descr = Strdup(desc);
    new_opt_table->personality = personality;
    new_opt_table->f = pdshpy_process_opt;

    bzero(&new_opt_table[1], sizeof(struct pdsh_module_option));

    if (!opt_register(new_opt_table))
    {
        PyErr_SetString(PyExc_ValueError,
                        "Pdsh refused to allow option to be registered");
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *
pdshpy_rcmd_register_defaults(PyObject *self, PyObject *args)
{
    const char *hostliststr = NULL;
    const char *rcmd_module_name = NULL;
    const char *username = NULL;
    char *nonconst_hostliststr = NULL;
    char *nonconst_rcmd_module_name = NULL;
    char *nonconst_username = NULL;
    int result = 0;

    if (!PyArg_ParseTuple(args, "zsz",
                          &hostliststr, &rcmd_module_name, &username))
        return NULL;

    /* pdsh source doesn't use const qualifiers, and I haven't dug deep enough
     * to be sure it's not going to mess with the contents of these strings */
    nonconst_hostliststr = Strdup(hostliststr);
    nonconst_rcmd_module_name = Strdup(rcmd_module_name);
    nonconst_username = Strdup(username);

    result = rcmd_register_defaults(nonconst_hostliststr,
                                    nonconst_rcmd_module_name,
                                    nonconst_username);

    Free((void **)(&nonconst_hostliststr));
    Free((void **)(&nonconst_rcmd_module_name));
    Free((void **)(&nonconst_username));

    if (result < 0)
    {
        if (hostliststr == NULL)
            hostliststr = "(null)";
        if (username == NULL)
            username = "(null)";
        PyErr_Format(PyExc_ValueError,
                     "Failed to register rcmd defaults for '%s', '%s', '%s'",
                     hostliststr, rcmd_module_name, username);
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *
make_pyobject_from_hostlist(hostlist_t hl)
{
    PyObject *pylist = NULL;
    PyObject *listitem = NULL;
    const char *item = NULL;
    hostlist_iterator_t hli = NULL;

    if (hl == NULL)
        Py_RETURN_NONE;

    if ((hli = hostlist_iterator_create(hl)) == NULL)
    {
        PyErr_SetString(PyExc_RuntimeError,
                        "Could not allocate hostlist iterator");
        return NULL;
    }

    if ((pylist = PyList_New(0)) == NULL)
    {
        hostlist_iterator_destroy(hli);
        return NULL;
    }

    for (item = hostlist_next(hli); item; item = hostlist_next(hli))
    {
        if ((listitem = PyString_FromString(item)) == NULL)
        {
            hostlist_iterator_destroy(hli);
            Py_DECREF(pylist);
            return NULL;
        }
        if (PyList_Append(pylist, listitem) < 0)
        {
            hostlist_iterator_destroy(hli);
            Py_DECREF(listitem);
            Py_DECREF(pylist);
            return NULL;
        }
        Py_DECREF(listitem);
    }

    hostlist_iterator_destroy(hli);
    return pylist;
}

static hostlist_t
make_hostlist_from_pyobject(PyObject *pylist)
{
    hostlist_t hl = NULL;
    PyObject *pyiter = NULL;
    PyObject *nexthost = NULL;
    PyObject *hoststrpy = NULL;
    const char *hoststr = NULL;

    if ((hl = hostlist_create(NULL)) == NULL)
    {
        PyErr_SetString(PyExc_RuntimeError, "Could not allocate hostlist");
        return NULL;
    }

    if (pylist == Py_None)
        return hl;

    if ((pyiter = PyObject_GetIter(pylist)) == NULL)
    {
        hostlist_destroy(hl);
        return NULL;
    }

    while ((nexthost = PyIter_Next(pyiter)) != NULL)
    {
        hoststrpy = PyObject_Str(nexthost);
        Py_DECREF(nexthost);
        if (hoststrpy == NULL)
            break;

        hoststr = PyString_AsString(hoststrpy);
        Py_DECREF(hoststrpy);
        if (hoststr == NULL)
            break;

        if (!hostlist_push_host(hl, hoststr))
        {
            PyErr_SetString(PyExc_RuntimeError, "Could not add to hostlist");
            break;
        }
    }

    Py_DECREF(pyiter);
    if (PyErr_Occurred())
    {
        hostlist_destroy(hl);
        return NULL;
    }

    return hl;
}

static PyObject *
PyString_FromStringOrNull(const char *str)
{
    if (str == NULL)
        Py_RETURN_NONE;
    return PyString_FromString(str);
}

static int
PyIntOrNone_AsLong(PyObject *pyint)
{
    if (pyint == NULL || pyint == Py_None)
        return 0;
    else
        return PyInt_AsLong(pyint);
}

static PyObject *
make_pyobject_from_pdsh_opt(opt_t *pdsh_opts)
{
    PyObject *pyopts = NULL;

    pyopts = PyObject_CallMethod(pymodule_util, "PdshOpts", NULL);
    if (pyopts == NULL)
        return NULL;

#define SETATTR(name, pyinitializer) ({                                 \
    if ((PyObject_SetAttrString(pyopts, #name,                          \
                                (pyinitializer)(pdsh_opts->name))) < 0) \
    {                                                                   \
        Py_DECREF(pyopts);                                              \
        return NULL;                                                    \
    }                                                                   \
})

#define SETATTR_INT(name)  SETATTR(name, PyInt_FromLong)
#define SETATTR_BOOL(name) SETATTR(name, PyBool_FromLong)

#define SETATTR_STR(name)  SETATTR(name, PyString_FromStringOrNull)

    SETATTR_STR(progname);
    SETATTR_BOOL(debug);
    SETATTR_BOOL(info_only);
    SETATTR_BOOL(test_range_expansion);
    SETATTR_BOOL(sdr_verify);
    SETATTR_BOOL(sdr_global);
    SETATTR_BOOL(altnames);
    SETATTR_BOOL(sigint_terminates);
    SETATTR_STR(luser);
    SETATTR_INT(luid);
    SETATTR_STR(ruser);
    SETATTR_INT(fanout);
    SETATTR_INT(connect_timeout);
    SETATTR_INT(command_timeout);

    SETATTR_STR(rcmd_name);
    SETATTR_STR(misc_modules);
    SETATTR_BOOL(resolve_hosts);

    SETATTR_BOOL(kill_on_fail);

    /* DSH-specific options */
    SETATTR_BOOL(separate_stderr);
    SETATTR_BOOL(stdin_unavailable);
    SETATTR_STR(cmd);
    SETATTR_STR(dshpath);
    SETATTR_STR(getstat);
    SETATTR_BOOL(ret_remote_rc);
    SETATTR_BOOL(labels);

    /* PCP-specific options */
    SETATTR_BOOL(preserve);
    SETATTR_BOOL(recursive);
    SETATTR_STR(outfile_name);
    SETATTR_BOOL(pcp_server);
    SETATTR_BOOL(target_is_directory);
    SETATTR_BOOL(pcp_client);
    SETATTR_STR(pcp_client_host);
    SETATTR_STR(local_program_path);
    SETATTR_STR(remote_program_path);
    SETATTR_BOOL(reverse_copy);

    SETATTR(wcoll, make_pyobject_from_hostlist);

    /* the one option attribute I didn't bother adding:
     *    List infile_names;
     *
     * I don't think I care about it right now.
     */

    return pyopts;
}

static int
fill_pdshopt_from_pyobject(opt_t *pdsh_opts, PyObject *pyopts)
{
    PyObject *val = NULL;
    PyObject *strval = NULL;

#define FILLATTR(name, pyextractor) ({                                  \
    val = PyObject_GetAttrString(pyopts, #name);                        \
    if (val == NULL)                                                    \
        return 0;                                                       \
    pdsh_opts->name = (pyextractor)(val);                               \
    Py_DECREF(val);                                                     \
    if (PyErr_Occurred())                                               \
        return 0;                                                       \
})

#define FILLATTR_INT(name)  FILLATTR(name, PyIntOrNone_AsLong)
#define FILLATTR_BOOL(name) FILLATTR(name, PyObject_IsTrue)

#define FILLATTR_STR(name) ({                                           \
    val = PyObject_GetAttrString(pyopts, #name);                        \
    if (val == NULL)                                                    \
        return 0;                                                       \
    if (val == Py_None)                                                 \
    {                                                                   \
        if (pdsh_opts->name != NULL)                                    \
            Free((void **)&(pdsh_opts->name));                          \
        Py_DECREF(val);                                                 \
    }                                                                   \
    else                                                                \
    {                                                                   \
        strval = PyObject_Bytes(val);                                   \
        Py_DECREF(val);                                                 \
        if (strval == NULL)                                             \
            return 0;                                                   \
        if (pdsh_opts->name == NULL)                                    \
            pdsh_opts->name = Strdup(PyBytes_AsString(strval));         \
        else if (strcmp(pdsh_opts->name, PyBytes_AsString(strval)))     \
        {                                                               \
            Free((void **)&(pdsh_opts->name));                          \
            pdsh_opts->name = Strdup(PyBytes_AsString(strval));         \
            Py_DECREF(strval);                                          \
            if (pdsh_opts->name == NULL)                                \
                return 0;                                               \
        }                                                               \
        Py_DECREF(strval);                                              \
    }                                                                   \
})

    FILLATTR_STR(progname);
    FILLATTR_BOOL(debug);
    FILLATTR_BOOL(info_only);
    FILLATTR_BOOL(test_range_expansion);
    FILLATTR_BOOL(sdr_verify);
    FILLATTR_BOOL(sdr_global);
    FILLATTR_BOOL(altnames);
    FILLATTR_BOOL(sigint_terminates);
    FILLATTR_STR(luser);
    FILLATTR_INT(luid);
    FILLATTR_STR(ruser);
    FILLATTR_INT(fanout);
    FILLATTR_INT(connect_timeout);
    FILLATTR_INT(command_timeout);

    FILLATTR_STR(rcmd_name);
    FILLATTR_STR(misc_modules);
    FILLATTR_BOOL(resolve_hosts);

    FILLATTR_BOOL(kill_on_fail);

    /* DSH-specific options */
    FILLATTR_BOOL(separate_stderr);
    FILLATTR_BOOL(stdin_unavailable);
    FILLATTR_STR(cmd);
    FILLATTR_STR(dshpath);
    FILLATTR_STR(getstat);
    FILLATTR_BOOL(ret_remote_rc);
    FILLATTR_BOOL(labels);

    /* PCP-specific options */
    FILLATTR_BOOL(preserve);
    FILLATTR_BOOL(recursive);
    FILLATTR_STR(outfile_name);
    FILLATTR_BOOL(pcp_server);
    FILLATTR_BOOL(target_is_directory);
    FILLATTR_BOOL(pcp_client);
    FILLATTR_STR(pcp_client_host);
    FILLATTR_STR(local_program_path);
    FILLATTR_STR(remote_program_path);
    FILLATTR_BOOL(reverse_copy);

    val = PyObject_GetAttrString(pyopts, "wcoll");
    if (val == NULL)
        return 0;
    hostlist_destroy(pdsh_opts->wcoll);
    if (val == Py_None)
        pdsh_opts->wcoll = NULL;
    else
    {
        pdsh_opts->wcoll = make_hostlist_from_pyobject(val);
        if (pdsh_opts->wcoll == NULL)
        {
            Py_DECREF(val);
            return 0;
        }
    }
    Py_DECREF(val);
    return 1;
}

static int
pdshpy_process_opt(opt_t *pdsh_opts, int opt, char *arg)
{
    PyObject *pyopt = NULL;
    PyObject *result = NULL;
    int result_int = 0;

    if ((pyopt = make_pyobject_from_pdsh_opt(pdsh_opts)) == NULL)
    {
        PYERR("Failed to construct PdshOpts object");
        return -1;
    }

    DBG("Calling process_option(%c, %s) in util module.", opt, arg);

    result = PyObject_CallMethod(pymodule_util, "process_option", "csOO",
                                 opt, arg, pyopt, pymodule_data);

    if (result == NULL)
    {
        Py_DECREF(pyopt);
        PYERR("Driver module's processing of option '%c' failed", opt);
        return -1;
    }

    if (!fill_pdshopt_from_pyobject(pdsh_opts, pyopt))
    {
        Py_DECREF(pyopt);
        Py_DECREF(result);
        PYERR("Driver module put invalid value in PdshOpts object");
        return -1;
    }
    Py_DECREF(pyopt);

    result_int = PyIntOrNone_AsLong(result);
    Py_DECREF(result);

    if (result_int < 0 && PyErr_Occurred())
    {
        PYERR("Driver module's processing for option '%c' did not return an int "
              "or None", opt);
    }
    return result_int;
}

static int
pdshpy_init(void)
{
    const char *debugenv = NULL;
    const char *modulename = NULL;
    PyObject *init_result = NULL;
    PyObject *initializer = NULL;

    debugenv = getenv(PDSHPY_ENVIRON_DEBUG);
    if (debugenv != NULL)
        debuglevel = atoi(debugenv);

    modulename = getenv(PDSHPY_ENVIRON_MODULENAME);
    if (modulename == NULL)
        modulename = PDSHPY_PYTHON_MODULE;

    Py_Initialize();

    DBG("Initializing internal module object");

    /* borrowed reference */
    pymodule_internal = Py_InitModule(PDSHPY_PYTHON_INTERNAL, pdshpy_methods);
    if (pymodule_internal == NULL)
    {
        PYERR("Failed to initialize internal module object");
        return -1;
    }

    DBG("Importing util module");

    pymodule_util = PyImport_ImportModule(PDSHPY_UTIL_MODULE);
    if (pymodule_util == NULL)
    {
        if (debuglevel > 0)
            PYERR("Failed to import util module " PDSHPY_UTIL_MODULE);
        return -1;
    }

    DBG("Loading driver module: %s", modulename);

    pymodule = PyImport_ImportModule(modulename);
    if (pymodule == NULL)
    {
        if (debuglevel > 0)
            PYERR("Failed to import driver module %s", modulename);
        Py_DECREF(pymodule_util);
        return -1;
    }

    DBG("Loaded driver module: %s", PyModule_GetFilename(pymodule));

    pymodule_data = PyObject_CallMethod(pymodule_util,
                                        "PdshpyModuleData", NULL);
    if (pymodule_data == NULL)
    {
        PYERR("Failed to instantiate PdshpyModuleData");
        Py_DECREF(pymodule);
        Py_DECREF(pymodule_util);
        return -1;
    }

    DBG("Calling initialize() in driver module.");

    initializer = PyObject_GetAttrString(pymodule, "initialize");
    if (initializer == NULL)
    {
        /* that's ok, it's optional */
        PyErr_Clear();
    }
    else
    {
        init_result = PyObject_CallFunction(initializer, "O", pymodule_data);
        Py_DECREF(initializer);

        if (init_result == NULL)
        {
            PYERR("Driver module's initialize() function failed");
            Py_DECREF(pymodule_data);
            Py_DECREF(pymodule);
            Py_DECREF(pymodule_util);
            return -1;
        }
        Py_DECREF(init_result);
    }

    DBG("Initialization complete.");

    return 0;
}

static int
pdshpy_fini(void)
{
    int i;

    DBG("Unloading.");

    Py_DECREF(pymodule_data);
    pymodule_data = NULL;
    Py_DECREF(pymodule);
    pymodule = NULL;
    Py_DECREF(pymodule_util);
    pymodule_util = NULL;
    pymodule_internal = NULL;

    for (i = 0; i < options_registered; ++i)
    {
        Free((void **)&pdsh_module_info.opt_table[i].arginfo);
        Free((void **)&pdsh_module_info.opt_table[i].descr);
    }
    if (options_registered > 0)
        free(pdsh_module_info.opt_table);

    pdsh_module_info.opt_table = &null_option;
    options_registered = 0;

    Py_Finalize();
    return 0;
}

/* Called after option processing, and before postop. Append all appropriate
 * host results onto opt->wcoll.
 */
static hostlist_t
pdshpy_wcoll(opt_t *opt)
{
    PyObject *hostlist = NULL;
    PyObject *pyopt = NULL;
    hostlist_t hl = NULL;

    if ((pyopt = make_pyobject_from_pdsh_opt(opt)) == NULL)
    {
        PYERR("Failed to construct PdshOpts object");
        return NULL;
    }

    DBG("Calling collect_hosts() in driver module.");

    hostlist = PyObject_CallMethod(pymodule, "collect_hosts", "OO",
                                   pyopt, pymodule_data);

    if (hostlist == NULL)
    {
        PYERR("Driver module collect_hosts() function failed");
        Py_DECREF(pyopt);
        return NULL;
    }

    if (!fill_pdshopt_from_pyobject(opt, pyopt))
    {
        Py_DECREF(hostlist);
        Py_DECREF(pyopt);
        PYERR("Driver module collect_hosts() function put an invalid value "
              "in a PdshOpts object.");
        return NULL;
    }
    Py_DECREF(pyopt);

    /* It's ok if this returns NULL; we're just going to return it anyway */
    hl = make_hostlist_from_pyobject(hostlist);
    Py_DECREF(hostlist);

    return hl;
}

/* Can be used to filter the "working collective", as in -v with nodeupdown,
 * or -i with genders. Returns the total number of errors.
 */
static int
pdshpy_postop(opt_t *opt)
{
    PyObject *result = NULL;
    PyObject *pyopt = NULL;
    int result_int = 0;

    if ((pyopt = make_pyobject_from_pdsh_opt(opt)) == NULL)
    {
        PYERR("Failed to construct PdshOpts object");
        return 1;
    }

    DBG("Calling perform_postop() in driver module.");

    result = PyObject_CallMethod(pymodule, "perform_postop", "OO",
                                 pyopt, pymodule_data);

    if (result == NULL)
    {
        Py_DECREF(pyopt);
        PYERR("Driver module perform_postop() function failed");
        return 1;
    }

    if (!fill_pdshopt_from_pyobject(opt, pyopt))
    {
        Py_DECREF(pyopt);
        Py_DECREF(result);
        PYERR("Driver module perform_postop() function put an invalid value "
              "in PdshOpts object.");
        return 1;
    }
    Py_DECREF(pyopt);

    result_int = PyIntOrNone_AsLong(result);
    Py_DECREF(result);

    if (result_int < 0)
    {
        if (PyErr_Occurred())
            PYERR("Value returned from driver module perform_postop() is not "
                  "an int or None (should be the number of errors)");
        else
        {
            /* the value returned was actually negative. that's an error too;
             * it's just that Python won't know what the error is */
            ERR("Value returned from Python module perform_postop method "
                "is negative (should be the number of errors)");
        }
        return 1;
    }

    return result_int;
}
