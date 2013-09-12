WWW_DIR     = quickusb

all ::  compile

compile:
	cd kernel; make ; cd -
	cd setquickusb; make ; cd -

.PHONY: www
www:
	rm -rf   www .www
	mkdir -p .www/$(WWW_DIR)/$(WWW_DIR)
	cp -r *  .www/$(WWW_DIR)/$(WWW_DIR)
	mv       .www www
	make -C  www/$(WWW_DIR)/$(WWW_DIR) clean
	tar -czf www/$(WWW_DIR)/$(WWW_DIR).tgz -C www/$(WWW_DIR) $(WWW_DIR)
	rm -rf   www/$(WWW_DIR)/$(WWW_DIR)
	cp       index.html README.txt www/$(WWW_DIR)/
	@echo "Now, upload www/$(WWW_DIR)/ and link to www/$(WWW_DIR)/index.html"

clean:
	cd kernel; make clean; cd -
	cd setquickusb; make clean; cd -
	rm -rf www/

install:
	@[ `whoami` = root ] || (echo "Error, please be root"; exit 1)
	cd kernel; make install; cd -
	cd setquickusb; make install; cd -

uninstall:
	@[ `whoami` = root ] || (echo "Error, please be root"; exit 1)
	cd kernel; make uninstall; cd -
	cd setquickusb; make uninstall; cd -




