# pdshpy interface module
#
# (bits that are easier to implement in straight python)

try:
    from _pdshpy_internal import _register_option, _rcmd_register_defaults
except ImportError:
    # allow module to be imported without error, for the sake of linting
    # and so on, even when not run under pdshpy proper.
    _register_option = _rcmd_register_defaults = None


class PdshOpts:
    # just a dumb class for setting a bunch of attributes on
    pass


class PdshpyModuleData:
    # and another one
    pass


# personality type constants as defined in pdsh/opt.h
DSH = 1
PCP = 2

# map of registered options and callbacks
_option_map = {}


def process_option(opt, arg, pdshopt, data):
    """
    Trampoline called by pdshpy internal code, which calls back into the
    appropriate python handler for an option given on the command line
    """
    result = _option_map[opt](opt, arg, pdshopt, data)
    if result is None:
        result = 0
    return result


def register_option(optletter, argmeta, personality, callback, desc=None):
    """
    Register a command line option for pdsh. This should be called during an
    initialize() function to have any useful effect.
    """
    if isinstance(personality, basestring):
        intpersonality = 0
        for p in personality.split(','):
            p = p.strip().upper()
            if p == 'DSH':
                intpersonality |= DSH
            elif p == 'PCP':
                intpersonality |= PCP
        personality = intpersonality
    _option_map[optletter] = callback
    return _register_option(optletter, argmeta, personality, desc)


def rcmd_register_defaults(hosts, rcmd_module, username=None):
    """
    Override pdsh's idea of which rcmd module (and, optionally, what username)
    should be used for a given list of hostnames. This will not override rcmd
    specifications given on the command line, or earlier settings by competing
    modules, but it will override defaults.

    @param hosts A comma-separated list of hostnames, or None. If None, the
                 specified rcmd module will become the new global default.
    @type hosts str
    @param rcmd_module The name of an rcmd module that should be used for the
                       given hosts.
    @type rcmd_module str
    @param username If specified, may be a string which specifies the remote
                    username to use for the given hostnames (or, if 'hosts'
                    is None, as the default). The actual meaning of remote
                    username may change based on the rcmd module chosen.
    @type username str
    """
    _rcmd_register_defaults(hosts, rcmd_module, username)
