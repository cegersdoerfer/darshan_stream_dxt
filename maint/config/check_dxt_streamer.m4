AC_DEFUN([CHECK_DXT_STREAMER],
[
AC_ARG_WITH(dxt-streamer,
[  --with-dxt-streamer=DIR root directory path of dxt streamer installation [defaults to
                    /usr/local or /usr if not found in /usr/local]
  --disable-dxt-streamer-mod to disable dxt streamer usage completely],
[if test -d "$withval"; then
    	DXT_STREAMER_HOME="$withval"
    	LDFLAGS="$LDFLAGS -L${DXT_STREAMER_HOME}/lib -Wl,-rpath=${DXT_STREAMER_HOME}/lib"
    	CPPFLAGS="$CPPFLAGS -I${DXT_STREAMER_HOME}/include"
    	__DARSHAN_DXT_STREAMER_LINK_FLAGS="-L${DXT_STREAMER_HOME}/lib"
    	__DARSHAN_DXT_STREAMER_INCLUDE_FLAGS="-I${DXT_STREAMER_HOME}/include"
    else
   	AC_MSG_ERROR(LDMS installation path is required)
fi])
])