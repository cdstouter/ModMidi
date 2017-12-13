FROM moddevices/mod-plugin-builder

# RUN sudo apt-get update
# RUN sudo apt-get install -qy build-essential checkinstall libreadline-gplv2-dev libncursesw5-dev libssl-dev libsqlite3-dev tk-dev libgdbm-dev libc6-dev libbz2-dev openssl libjack-dev libjansson-dev
COPY libjansson.so.4.7.0 /home/builder/mod-workdir/plugins-dep/host/usr/arm-buildroot-linux-gnueabihf/sysroot/usr/lib/libjansson.so.4.7.0
RUN cd /home/builder/mod-workdir/plugins-dep/host/usr/arm-buildroot-linux-gnueabihf/sysroot/usr/lib && ln -s libjansson.so.4.7.0 libjansson.so.4 && ln -s libjansson.so.4.7.0 libjansson.so
COPY jansson.h /home/builder/mod-workdir/plugins-dep/host/usr/arm-buildroot-linux-gnueabihf/sysroot/usr/include/jansson.h
COPY jansson_config.h /home/builder/mod-workdir/plugins-dep/host/usr/arm-buildroot-linux-gnueabihf/sysroot/usr/include/jansson_config.h
COPY jansson.pc /home/builder/mod-workdir/plugins-dep/host/usr/arm-buildroot-linux-gnueabihf/sysroot/usr/lib/pkgconfig/jansson.pc
RUN echo "source /home/builder/mod-plugin-builder/local.env" >> $HOME/.bashrc
CMD ["bash"]