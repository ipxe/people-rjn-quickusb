WWW_DIR     = quickusb
WWW_SERV    = www:public_html/src/

all ::  compile

compile:
	cd kernel; make ; cd -
	cd setquickusb; make ; cd -

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

.PHONY: wwwpublish
wwwpublish: www
	@echo "Uploading to web for publication..."
	scp -r www/$(WWW_DIR)/  $(WWW_SERV)

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




