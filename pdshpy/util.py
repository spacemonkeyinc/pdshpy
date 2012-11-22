# pdshpy interface module
#
# (bits that are easier to implement in straight python)

from _pdshpy_internal import _register_option


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
    result = _option_map[opt](opt, arg, pdshopt, data)
    if result is None:
        result = 0
    return result


def register_option(optletter, argmeta, personality, callback, desc=None):
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
