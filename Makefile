MODULE = pdshpy
PDSH_HEADERS = pdsh_headers
PYTHON = python2.7
PDSH_MODULE_DIR = /usr/lib/pdsh
DESTDIR = /

PYTHON_HEADERS=/usr/include/$(PYTHON)

CFLAGS += -pthread -Wall -fno-strict-aliasing -g -fwrapv -O2 -fPIC
CPPFLAGS += -DNDEBUG -I$(PDSH_HEADERS) -I$(PYTHON_HEADERS)
LDFLAGS += -Xlinker -export-dynamic -Wl,-O1 -Wl,-Bsymbolic-functions -l$(PYTHON)

all: $(MODULE).so

$(MODULE).so: $(MODULE).o
	$(CC) $(CFLAGS) -shared $(LDFLAGS) -o $@ $^

install:
	install -o root -g root -d $(DESTDIR)/$(PDSH_MODULE_DIR)
	install -m 644 -o root -g root $(MODULE).so $(DESTDIR)$(PDSH_MODULE_DIR)
	$(PYTHON) setup.py install --root $(DESTDIR) $(PYTHON_INSTALL_PARAMS)

clean:
	$(RM) $(MODULE).so $(MODULE).o
	$(RM) -r build

.PHONY: clean all install
