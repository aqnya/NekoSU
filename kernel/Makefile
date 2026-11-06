# SPDX-License-Identifier: GPL-3.0-or-later
MDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
IDIR := $(MDIR)/include

$(info -- KDIR: $(KDIR))
$(info -- MDIR: $(MDIR))
$(info -- IDIR: $(IDIR))

include $(MDIR)/Kbuild

ifeq ($(KERNELRELEASE),)

.PHONY: all clean format

all:
	$(MAKE) -C $(KDIR) M=$(MDIR) IDIR=$(IDIR) modules


clean:
	$(MAKE) -C $(KDIR) M=$(MDIR) clean

format:
	@files=$$(find $(MDIR) \
		-type d \( -name .git -o -name .tmp_versions \) -prune -o \
		-type f \( -name '*.c' -o -name '*.h' \) -print); \
	if [ -n "$$files" ]; then \
		echo "Formatting:"; echo "$$files"; \
		find $(MDIR) \
			-type d \( -name .git -o -name .tmp_versions \) -prune -o \
			-type f \( -name '*.c' -o -name '*.h' \) -print0 \
			| xargs -0 clang-format -i; \
		echo "Done."; \
	else \
		echo "No source files found."; \
	fi
	
endif
