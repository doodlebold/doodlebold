#include "core/io/JpegCodec.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>

extern "C" {
#include <jpeglib.h>
}

namespace doodlebold::io {

namespace {

// libjpeg reports errors by calling error_exit, which must not return. Throwing through C frames is not portable, so the error
// handler jumps back to a setjmp in encodeRaw(), the only function that calls libjpeg and, deliberately, holds nothing with a
// destructor.
struct ErrorManager {
    jpeg_error_mgr pub;
    std::jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
};

void onError(j_common_ptr cinfo) {
    auto* err = reinterpret_cast<ErrorManager*>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, err->message);
    std::longjmp(err->jump, 1);  // NOLINT(modernize-avoid-setjmp-longjmp): libjpeg's way out of an error; it is C and can't unwind
}

void onMessage(j_common_ptr, int) {}

bool encodeRaw(const unsigned char* rgb, int width, int height, std::size_t stride, const JpegSettings& s, unsigned char** out,
               unsigned long* size, char* message) {
    jpeg_compress_struct cinfo{};
    ErrorManager err{};
    cinfo.err = jpeg_std_error(&err.pub);
    err.pub.error_exit = onError;
    err.pub.emit_message = onMessage;
    err.message[0] = '\0';
    *out = nullptr;
    *size = 0;
    if (setjmp(err.jump)) {  // NOLINT(modernize-avoid-setjmp-longjmp): where onError comes back to
        jpeg_destroy_compress(&cinfo);
        std::free(*out);  // NOLINT(cppcoreguidelines-no-malloc): jpeg_mem_dest allocated it with malloc
        *out = nullptr;
        std::snprintf(message, JMSG_LENGTH_MAX, "%s", err.message);
        return false;
    }
    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, out, size);
    cinfo.image_width = static_cast<JDIMENSION>(width);
    cinfo.image_height = static_cast<JDIMENSION>(height);
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, std::clamp(s.quality, 1, 100), TRUE);  // TRUE: baseline-safe tables (entries ≤ 255)
    jpeg_set_colorspace(&cinfo, JCS_YCbCr);
    const JpegSampling sampling = samplingFor(s);
    cinfo.comp_info[0].h_samp_factor = sampling.horizontal;
    cinfo.comp_info[0].v_samp_factor = sampling.vertical;
    cinfo.comp_info[1].h_samp_factor = cinfo.comp_info[1].v_samp_factor = 1;
    cinfo.comp_info[2].h_samp_factor = cinfo.comp_info[2].v_samp_factor = 1;
    cinfo.optimize_coding = FALSE;  // the standard Huffman tables, as a baseline writer without a second pass
    cinfo.write_JFIF_header = TRUE;
    cinfo.JFIF_major_version = 1;
    cinfo.JFIF_minor_version = 2;
    cinfo.density_unit = 1;
    const auto density = static_cast<UINT16>(std::clamp(std::lround(s.resolution), 1L, 65535L));
    cinfo.X_density = cinfo.Y_density = density;
    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = const_cast<JSAMPROW>(rgb + static_cast<std::size_t>(cinfo.next_scanline) * stride);
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    return true;
}

}

JpegSampling samplingFor(const JpegSettings& settings) {
    return {settings.chromaHorizontal ? 2 : 1, settings.chromaVertical ? 2 : 1};
}

FileResult<QByteArray> encodeJpeg(const QImage& picture, const JpegSettings& settings) {
    using R = FileResult<QByteArray>;
    if (picture.isNull() || picture.width() > 65500 || picture.height() > 65500)
        return R::failure({FileError::Kind::TooLarge, {}});
    const QImage rgb = picture.format() == QImage::Format_RGB888 ? picture : picture.convertToFormat(QImage::Format_RGB888);
    if (rgb.isNull())
        return R::failure({FileError::Kind::TooLarge, {}});
    unsigned char* buffer = nullptr;
    unsigned long size = 0;
    char message[JMSG_LENGTH_MAX];
    if (!encodeRaw(rgb.constBits(), rgb.width(), rgb.height(), static_cast<std::size_t>(rgb.bytesPerLine()), settings, &buffer, &size,
                   message))
        return R::failure({FileError::Kind::CannotWrite, QString::fromLatin1(message)});
    QByteArray data(reinterpret_cast<const char*>(buffer), static_cast<qsizetype>(size));
    std::free(buffer);  // NOLINT(cppcoreguidelines-no-malloc): as above
    return data;
}

}
