#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <stdexcept>

#include <utki/config.hpp>

#include <r4/matrix2.hpp>
#include <r4/rectangle.hpp>

#include <svgdom/elements/styleable.hpp>
#include <svgdom/elements/gradients.hpp>

#include "config.hxx"
#include "surface.hxx"

#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
#	if M_OS == M_OS_WINDOWS || M_OS_NAME == M_OS_NAME_IOS
#		include <cairo.h>
#	else
#		include <cairo/cairo.h>
#	endif
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
#	include <agg2/agg_rendering_buffer.h>
#	include <agg2/agg_pixfmt_rgba.h>
#	include <agg2/agg_renderer_base.h>
#	include <agg2/agg_renderer_scanline.h>
#	include <agg2/agg_path_storage.h>
#	include <agg2/agg_scanline_u.h>
#	include <agg2/agg_rasterizer_scanline_aa.h>
#	include <agg2/agg_curves.h>
#	include <agg2/agg_conv_stroke.h>
#	include <agg2/agg_gradient_lut.h>
#	include <agg2/agg_span_gradient.h>
#	include <agg2/agg_span_allocator.h>
#endif

namespace svgren{

inline r4::vector4<unsigned> get_rgba(uint32_t c){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	// cairo uses BGRA format
	return {
		unsigned((c >> 16) & 0xff),
		unsigned((c >> 8) & 0xff),
		unsigned((c >> 0) & 0xff),
		unsigned((c >> 24) & 0xff)
	};
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	// RGBA format
	return {
		unsigned((c >> 0) & 0xff),
		unsigned((c >> 8) & 0xff),
		unsigned((c >> 16) & 0xff),
		unsigned((c >> 24) & 0xff)
	};
#endif
}

inline uint32_t get_uint32_t(const r4::vector4<unsigned>& rgba){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	// cairo uses BGRA format
	return rgba.b() | (rgba.g() << 8) | (rgba.r() << 16) | (rgba.a() << 24);
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	// RGBA format
	return rgba.r() | (rgba.g() << 8) | (rgba.b() << 16) | (rgba.a() << 24);
#endif
}

class canvas{
	std::vector<uint32_t> pixels;

public:
	class gradient{
		friend class canvas;
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	protected:
		cairo_pattern_t* pattern;
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
		struct gradient_wrapper_base{
			virtual int calculate(int x, int y, int) const = 0;
			virtual ~gradient_wrapper_base(){}
		};
		template<class T> struct gradient_wrapper : public gradient_wrapper_base{
			T g;
			int calculate(int x, int y, int d)const override{
				return this->g.calculate(x, y, d);
			}
		};

		gradient_wrapper_base* grad;

		agg::gradient_lut<agg::color_interpolator<agg::rgba8>, 1024> lut;
#endif
	public:

		struct stop{
			r4::vector4<real> rgba;
			real offset;
		};

		void set_spread_method(svgdom::gradient::spread_method spread_method);
		void set_stops(utki::span<const stop> stops);
	};

	class linear_gradient : public gradient{
#if SVGREN_BACKEND == SVGREN_BACKEND_AGG
		gradient_wrapper<agg::gradient_x> linear_grad;
#endif
	public:
		linear_gradient(
				const r4::vector2<real>& p0,
				const r4::vector2<real>& p1
			);
	};

	class radial_gradient : public gradient{
#if SVGREN_BACKEND == SVGREN_BACKEND_AGG
		gradient_wrapper<agg::gradient_radial_focus> radial_grad;
#endif
	public:
		radial_gradient(const r4::vector2<real>& f, const r4::vector2<real>& c, real r);
	};

private:
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	typedef double backend_real;

	struct cairo_surface_wrapper{
		cairo_surface_t* surface;

		cairo_surface_wrapper(unsigned width, unsigned height, uint32_t* buffer){
			if(width == 0 || height == 0){
				throw std::invalid_argument("svgren::canvas::canvas(): width or height argument is zero");
			}
			int stride = width * sizeof(uint32_t);
			this->surface = cairo_image_surface_create_for_data(
				reinterpret_cast<unsigned char*>(buffer),
				CAIRO_FORMAT_ARGB32,
				width,
				height,
				stride
			);
			if(!this->surface){
				throw std::runtime_error("svgren::canvas::canvas(): could not create cairo surface");
			}
		}
		~cairo_surface_wrapper(){
			cairo_surface_destroy(this->surface);
		}
	} surface;

	cairo_t* cr;
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	typedef double backend_real;

	typedef agg::curve3_div agg_curve3_type;
	typedef agg::curve4_div agg_curve4_type;

	const real approximation_scale = 5;

	agg::rendering_buffer rendering_buffer;
	agg::pixfmt_rgba32 pixel_format;
	agg::renderer_base<decltype(pixel_format)> renderer_base;
	agg::renderer_scanline_aa_solid<decltype(renderer_base)> renderer;
	agg::rasterizer_scanline_aa<> rasterizer;
	agg::scanline_u8 scanline;

	agg::path_storage path;

	agg::span_allocator<decltype(pixel_format)::color_type> span_allocator;

	struct context_type{
		agg::trans_affine matrix; // right after construction it is set to identity matrix
		agg::rgba color = agg::rgba(0);
		std::shared_ptr<const gradient> grad;
		real line_width = 1;
		agg::filling_rule_e fill_rule = agg::filling_rule_e::fill_even_odd;
		agg::line_cap_e line_cap = agg::line_cap_e::butt_cap;
		agg::line_join_e line_join = agg::line_join_e::miter_join;
	} context;

	std::vector<context_type> context_stack;
#endif

public:

	canvas(unsigned width, unsigned height);
	~canvas();

	void transform(const r4::matrix2<real>& matrix);
	void translate(real x, real y);
	void translate(const r4::vector2<real>& v){
		this->translate(v.x(), v.y());
	}
	void rotate(real radians);
	void scale(real x, real y);
	void scale(const r4::vector2<real>& v){
		this->scale(v.x(), v.y());
	}

	void set_fill_rule(svgdom::fill_rule fr);

	void set_source(const r4::vector4<real>& rgba);
	void set_source(std::shared_ptr<const linear_gradient> g);
	void set_source(std::shared_ptr<const radial_gradient> g);

	r4::vector2<real> matrix_mul(const r4::vector2<real>& v);

	// multiply by current matrix without translation part
	r4::vector2<real> matrix_mul_distance(const r4::vector2<real>& v);

	r4::rectangle<real> get_shape_bounding_box()const;

	r4::vector2<real> get_current_point()const;

	void move_to_abs(const r4::vector2<real>& p);
	void move_to_rel(const r4::vector2<real>& p);

	void line_to_abs(const r4::vector2<real>& p);
	void line_to_rel(const r4::vector2<real>& p);

	void quadratic_curve_to_abs(const r4::vector2<real>& cp1, const r4::vector2<real>& ep);
	void quadratic_curve_to_rel(const r4::vector2<real>& cp1, const r4::vector2<real>& ep);

	void cubic_curve_to_abs(const r4::vector2<real>& cp1, const r4::vector2<real>& cp2, const r4::vector2<real>& ep);
	void cubic_curve_to_rel(const r4::vector2<real>& cp1, const r4::vector2<real>& cp2, const r4::vector2<real>& ep);

	void arc_abs(const r4::vector2<real>& center, const r4::vector2<real>& radius, real start_angle, real sweep_angle);

	void arc_abs(const r4::vector2<real>& end_point, const r4::vector2<real>& radius, real x_axis_rotation, bool large_arc, bool sweep);
	void arc_rel(const r4::vector2<real>& end_point, const r4::vector2<real>& radius, real x_axis_rotation, bool large_arc, bool sweep);

	void close_path();

	void clear_path();

	void fill();
	void stroke();

	void set_line_width(real width);
	void set_line_cap(svgdom::stroke_line_cap lc);
	void set_line_join(svgdom::stroke_line_join lj);

	void rectangle(const r4::rectangle<real>& rect);

	void push_context();
	void pop_context();

	r4::matrix2<real> get_matrix();
	void set_matrix(const r4::matrix2<real>& m);

	void push_group();
	void pop_group(real opacity);
	void pop_mask_and_group();

	svgren::surface get_sub_surface(const r4::rectangle<unsigned>& region = {0, std::numeric_limits<unsigned>::max()});

	std::vector<uint32_t> release();
};

}
