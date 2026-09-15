# Copyright 1999-2025 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

inherit flag-o-matic meson optfeature xdg

DESCRIPTION="A lightweight GTK image viewer forked from GQview (stripped-down fork)"
HOMEPAGE="https://github.com/installgentoo/geeqie"

COMMIT="2b2feccf40a3fee048b550443fe1d9aa054835ac"
SRC_URI="https://github.com/installgentoo/${PN}/archive/${COMMIT}.tar.gz -> ${P}.tar.gz"
KEYWORDS="~amd64 ~arm64 ~x86"
S="${WORKDIR}/${PN}-${COMMIT}"

LICENSE="GPL-2"
SLOT="0"
IUSE="debug djvu ffmpegthumbnailer heif jpeg jpeg2k jpegxl pdf tiff webp X"

RDEPEND="virtual/libintl
	x11-libs/gtk+:3[X?]
	>=media-gfx/exiv2-0.17:=
	djvu? ( app-text/djvu )
	ffmpegthumbnailer? ( media-video/ffmpegthumbnailer )
	heif? ( >=media-libs/libheif-1.3.2 )
	jpeg2k? ( >=media-libs/openjpeg-2.3.0:2= )
	jpeg? ( media-libs/libjpeg-turbo:= )
	jpegxl? ( >=media-libs/libjxl-0.3.7:= )
	pdf? ( >=app-text/poppler-0.62[cairo] )
	tiff? ( media-libs/tiff:= )
	webp? ( >=media-libs/libwebp-0.6.1:= )"
DEPEND="${RDEPEND}"
BDEPEND="
	dev-util/glib-utils
	sys-devel/gettext
	virtual/pkgconfig"

src_prepare() {
	default

	# The ancillary-files test checks for upstream files this fork removed
	sed -e "/^# Ancillary files test/,/^summary({'Ancillary files'/d" -i meson.build || die
}

src_configure() {
	# defang automagic dependencies
	# Currently only needed for X11-specific workarounds.
	use X || append-flags -DGENTOO_GTK_HIDE_X11

	local emesonargs=(
		$(meson_use debug)
		$(meson_feature djvu)
		$(meson_feature ffmpegthumbnailer videothumbnailer)
		$(meson_feature heif)
		$(meson_feature jpeg)
		$(meson_feature jpeg2k j2k)
		$(meson_feature jpegxl)
		$(meson_feature pdf)
		$(meson_feature tiff)
		$(meson_feature webp)
	)

	# Bug: https://bugs.gentoo.org/957023
	# https://github.com/BestImageViewer/geeqie/issues/1762
	#
	# Fixed in git master, remove for 2.7
	filter-lto
	meson_src_configure
}

pkg_postinst() {
	xdg_pkg_postinst

	optfeature "Camera import and tethered photography plugins" media-gfx/gphoto2
	optfeature "Image crop plugin" "media-libs/exiftool media-gfx/imagemagick"
	optfeature "Image rotate plugin (JPEG)" media-gfx/fbida
	optfeature "Image rotate plugin (TIFF/PNG)" media-gfx/imagemagick
	optfeature "Video similarity in the duplicates window" media-video/ffmpeg
}
