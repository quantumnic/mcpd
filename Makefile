.PHONY: test clean

test:
	@$(MAKE) -C test/native test

clean:
	@$(MAKE) -C test/native clean
