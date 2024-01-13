// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2023 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

#include "pdf-image-source.h"

#include <cairo.h>
#include <poppler.h>
#include <stdlib.h>

#include <algorithm>
#include <filesystem>

#include "framebuffer.h"

namespace fs = std::filesystem;

namespace timg {

std::string PDFImageSource::FormatTitle(
    const std::string &format_string) const {
    return FormatFromParameters(format_string, filename_, (int)orig_width_,
                                (int)orig_height_, "pdf");
}

bool PDFImageSource::LoadAndScale(const DisplayOptions &opts, int, int) {
    options_        = opts;

    GError *error = nullptr;

    // Poppler wants a URI as input.
    std::string uri = "file://" + fs::absolute(filename_).string();

    PopplerDocument *document =
        poppler_document_new_from_file(uri.c_str(), nullptr, &error);
    if (!document) {
        fprintf(stderr, "no dice %s\n", error->message);
        return false;
    }

    const int page_count = poppler_document_get_n_pages(document);

    for (int page_num = 0; page_num < page_count; ++page_num) {
        PopplerPage *page = poppler_document_get_page(document, page_num);
        if (page == nullptr) {
            return false;
        }

        poppler_page_get_size(page, &orig_width_, &orig_height_);
        int target_width;
        int target_height;
        CalcScaleToFitDisplay(orig_width_, orig_height_, opts, false,
                              &target_width, &target_height);

        int render_width  = target_width;
        int render_height = target_height;

        const auto kCairoFormat = CAIRO_FORMAT_ARGB32;
        int stride = cairo_format_stride_for_width(kCairoFormat, render_width);
        std::unique_ptr<timg::Framebuffer> image(
            new timg::Framebuffer(stride / 4, render_height));

        cairo_surface_t *surface = cairo_image_surface_create_for_data(
            (uint8_t *)image->begin(), kCairoFormat, render_width,
            render_height, stride);
        cairo_t *cr = cairo_create(surface);
        cairo_scale(cr, 1.0 * render_width / orig_width_,
                    1.0 * render_height / orig_height_);
        cairo_save(cr);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);

        poppler_page_render(page, cr);
        cairo_restore(cr);
        g_object_unref(page);

        // render
        cairo_destroy(cr);
        cairo_surface_destroy(surface);

        // Cairo stores A (high-byte), R, G, B (low-byte). We need ABGR.
        for (rgba_t &pixel : *image) {
            std::swap(pixel.r, pixel.b);
        }

        // TODO: implement auto-crop and crop-border
        pages_.emplace_back(std::move(image));
    }

    return true;
}

int PDFImageSource::IndentationIfCentered(
    const timg::Framebuffer &image) const {
    return options_.center_horizontally ? (options_.width - image.width()) / 2
                                        : 0;
}

void PDFImageSource::SendFrames(const Duration &duration, int loops,
                                const volatile sig_atomic_t &interrupt_received,
                                const Renderer::WriteFramebufferFun &sink) {
    for (const auto &page : pages_) {
        const int dx = IndentationIfCentered(*page);
        sink(dx, 0, *page, SeqType::FrameImmediate,  {});
    }
}

}  // namespace timg