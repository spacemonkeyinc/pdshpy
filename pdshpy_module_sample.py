from pdshpy import util


"""
sample pdshpy_module to be used with pdshpy.

To use it, this module would need to be importable by Python as "pdshpy_module"
when pdsh starts.

All of the functions which will be called from pdshpy accept a "session"
parameter, on which arbitrary attributes can be set. All functions during one
pdsh session will get the same session object, so it's a reasonable way to keep
state aside from global variables.

Most functions also take a "pdsh_opts" parameter, which has attributes
representing the current set of options pdsh is working under. The module is
allowed to change these, as long as it makes sense, and the changes will be
propagated to the actual option struct. (no guarantees that the wrong values
won't make pdsh segfault or die). See struct opt_t in the pdsh source,
src/pdsh/opt.h, to see what attrs to expect.
"""


def initialize(session):
    """
    Called by pdsh when this module is loaded. This method is optional.
    Returned value is ignored.
    """

    # add a couple command-line options to pdsh for use by this module.
    # yes, they have to be one-character options (pdsh is kinda old-fashioned
    # like that, I guess). If an argument is expected with the option, give a
    # meta-name for the argument in the second parameter. The third param
    # lists the pdsh modes where the option should apply (DSH and PCP are the
    # only modes). The fourth argument is (duh) the callback which should
    # process the option when used, and the fifth is some descriptive text.
    util.register_option('Z', 'stuff', 'DSH, PCP', say_stuff,
                         'Echo the given stuff to stdout.')
    util.register_option('y', None, 'DSH, PCP', include_bruce,
                         'Include the hostname "bruce" in the working set.')
    session.extra_hosts_we_want_to_include = []


def say_stuff(opt, arg, pdsh_opts, session):
    """
    This is a silly callback registered by initialize(), above. The opt param
    will have 'Z' in it, and arg will have the argument given with with -Z on
    the command line.

    This should return None, or an integer indicating success (negative for
    failure).
    """
    print arg


def include_bruce(opt, arg, pdsh_opts, session):
    """
    Another silly callback. Here, opt will be 'y' and arg will be None.
    """
    session.extra_hosts_we_want_to_include.append('bruce')


def collect_hosts(pdsh_opts, session):
    """
    Called by pdsh after all option processing is done. Should return an
    iterable containing all node names that this module wants to include
    in the working set, or None to do nothing.
    """
    return session.extra_hosts_we_want_to_include


def perform_postop(pdsh_opts, session):
    """
    Called by pdsh after collecting hosts from all modules, but before actually
    doing anything remotely. This is where most modules do 'exclusions';
    removing things from the working set as instructed by the user.

    To remove (or add, whatever) hosts from the working set, change the 'wcoll'
    attribute on the pdsh_opts object.

    If this function wants to return something, it should return an int
    corresponding to the number of errors encountered.
    """
    if pdsh_opts.wcoll is None:
        return
    pdsh_opts.wcoll = set(pdsh_opts.wcoll)
    # this module hates nodes named "perl"
    pdsh_opts.wcoll.discard('perl')
