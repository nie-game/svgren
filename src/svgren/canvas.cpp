#include "canvas.hxx"

#include <algorithm>

#include <utki/debug.hpp>

#if SVGREN_BACKEND == SVGREN_BACKEND_AGG
#	include <agg2/agg_conv_stroke.h>
#	include <agg2/agg_curves.h>
#endif

using namespace svgren;

canvas::canvas(unsigned width, unsigned height) :
		pixels(
				width * height,
#ifdef M_SVGREN_BACKGROUND
				M_SVGREN_BACKGROUND
#else
				0
#endif
			)
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
		, surface(width, height, this->pixels.data())
		, cr(cairo_create(this->surface.surface))
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
		, rendering_buffer(reinterpret_cast<agg::int8u*>(pixels.data()), width, height, width * sizeof(decltype(this->pixels)::value_type))
		, pixel_format(this->rendering_buffer)
		, renderer_base(this->pixel_format)
		, renderer(renderer_base)
#endif
{
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	if(!this->cr){
		throw std::runtime_error("svgren::canvas::canvas(): could not create cairo context");
	}
#endif
}

canvas::~canvas(){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_destroy(this->cr);
#endif
}

std::vector<uint32_t> canvas::release(){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	for(auto &c : this->pixels){
		// swap red and blue channels, as cairo works in BGRA format, while we need to return RGBA
		c = (c & 0xff00ff00) | ((c << 16) & 0xff0000) | ((c >> 16) & 0xff);

		// unpremultiply alpha
		uint32_t a = (c >> 24);
		if(a == 0xff){
			continue;
		}
		if(a != 0){
			using std::min;
			uint32_t r = (c & 0xff) * 0xff / a;
			r = min(r, uint32_t(0xff)); // clamp top
			uint32_t g = ((c >> 8) & 0xff) * 0xff / a;
			g = min(g, uint32_t(0xff)); // clamp top
			uint32_t b = ((c >> 16) & 0xff) * 0xff / a;
			b = min(b, uint32_t(0xff)); // clamp top
			c = ((a << 24) | (b << 16) | (g << 8) | r);
		}else{
			c = 0;
		}
	}
#endif

	return std::move(this->pixels);
}

#if SVGREN_BACKEND == SVGREN_BACKEND_AGG
namespace{
agg::trans_affine to_agg_matrix(const r4::matrix2<real>& matrix){
	agg::trans_affine m;
	m.sx = matrix[0][0];  m.shx = matrix[0][1]; m.tx = matrix[0][2];
	m.shy = matrix[1][0]; m.sy = matrix[1][1];  m.ty = matrix[1][2];
	return m;
}
}

namespace{
r4::matrix2<real> to_r4_matrix(const agg::trans_affine& matrix){
	return {
			{real(matrix.sx),  real(matrix.shx), real(matrix.tx)},
			{real(matrix.shy), real(matrix.sy),  real(matrix.ty)}
		};
}
}
#endif

void canvas::transform(const r4::matrix2<real>& matrix){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_matrix_t m;
	m.xx = matrix[0][0];
	m.yx = matrix[1][0];
	m.xy = matrix[0][1];
	m.yy = matrix[1][1];
	m.x0 = matrix[0][2];
	m.y0 = matrix[1][2];
	cairo_transform(this->cr, &m);
	ASSERT_INFO(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS, "cairo status = " << cairo_status_to_string(cairo_status(this->cr)))
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	this->context.matrix *= to_agg_matrix(matrix);
#endif
}

void canvas::translate(real x, real y){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_translate(this->cr, x, y);
	ASSERT_INFO(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS, "cairo status = " << cairo_status_to_string(cairo_status(this->cr)))
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	this->context.matrix *= agg::trans_affine_translation(x, y);
#endif
}

void canvas::rotate(real radians){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_rotate(this->cr, radians);
	ASSERT_INFO(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS, "cairo status = " << cairo_status_to_string(cairo_status(this->cr)))
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	this->context.matrix *= agg::trans_affine_rotation(radians);
#endif
}

void canvas::scale(real x, real y){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	if(x * y != 0){ // cairo does not allow non-invertible scaling
		cairo_scale(this->cr, x, y);
		ASSERT_INFO(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS, "cairo status = " << cairo_status_to_string(cairo_status(this->cr)))
	}else{
		TRACE(<< "WARNING: non-invertible scaling encountered" << std::endl)
	}
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	this->context.matrix *= agg::trans_affine_scaling(x, y);
#endif
}

void canvas::set_fill_rule(canvas::fill_rule fr){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_fill_rule_t cfr;
	switch (fr){
		default:
			ASSERT(false);
		case fill_rule::even_odd:
			cfr = CAIRO_FILL_RULE_EVEN_ODD;
			break;
		case fill_rule::winding:
			cfr = CAIRO_FILL_RULE_WINDING;
			break;
	}
	cairo_set_fill_rule(this->cr, cfr);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#endif
}

void canvas::set_source(const r4::vector4<real>& rgba){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_set_source_rgba(this->cr, double(rgba.r()), double(rgba.g()), double(rgba.b()), double(rgba.a()));
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	this->context.color.r = rgba.r();
	this->context.color.g = rgba.g();
	this->context.color.b = rgba.b();
	this->context.color.a = rgba.a();
#endif
}

#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
namespace{
void set_cairo_pattern_properties(cairo_pattern_t* pat, const gradient& g){
	for(auto& s : g.stops){
		cairo_pattern_add_color_stop_rgba(pat, s.offset, s.rgba.r(), s.rgba.g(), s.rgba.b(), s.rgba.a());
		ASSERT(cairo_pattern_status(pat) == CAIRO_STATUS_SUCCESS)
	}

	cairo_extend_t extend;

	switch(g.spread_method){
		default:
		case svgdom::gradient::spread_method::default_:
			ASSERT(false)
		case svgdom::gradient::spread_method::pad:
			extend = CAIRO_EXTEND_PAD;
			break;
		case svgdom::gradient::spread_method::reflect:
			extend = CAIRO_EXTEND_REFLECT;
			break;
		case svgdom::gradient::spread_method::repeat:
			extend = CAIRO_EXTEND_REPEAT;
			break;
	}

	cairo_pattern_set_extend(pat, extend);
	ASSERT(cairo_pattern_status(pat) == CAIRO_STATUS_SUCCESS)
}
}
#endif

void canvas::set_source(const linear_gradient& g){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	auto pat = cairo_pattern_create_linear(
			double(g.p0.x()),
			double(g.p0.y()),
			double(g.p1.x()),
			double(g.p1.y())
		);
	if(!pat){
		throw std::runtime_error("cairo_pattern_create_linear() failed");
	}
	utki::scope_exit pat_scope_exit([&pat](){cairo_pattern_destroy(pat);});

	set_cairo_pattern_properties(pat, g);

	cairo_set_source(this->cr, pat);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#endif
}

void canvas::set_source(const radial_gradient& g){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	auto pat = cairo_pattern_create_radial(
			double(g.c0.x()),
			double(g.c0.y()),
			double(g.r0),
			double(g.c1.x()),
			double(g.c1.y()),
			double(g.r1)
		);
	if(!pat){
		throw std::runtime_error("cairo_pattern_create_radial() failed");
	}
	utki::scope_exit pat_scope_exit([&pat](){cairo_pattern_destroy(pat);});

	set_cairo_pattern_properties(pat, g);

	cairo_set_source(this->cr, pat);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#endif
}

r4::vector2<real> canvas::matrix_mul(const r4::vector2<real>& v){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	double x = v.x();
	double y = v.y();
	cairo_user_to_device(this->cr, &x, &y);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
	return r4::vector2<real>(real(x), real(y));
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	auto vec = v.to<decltype(this->context.matrix.sx)>();
	this->context.matrix.transform(&vec.x(), &vec.y());
	return vec.to<real>();
#endif
}

r4::vector2<real> canvas::matrix_mul_distance(const r4::vector2<real>& v){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	double x = v.x();
	double y = v.y();
	cairo_user_to_device_distance(this->cr, &x, &y);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
	return r4::vector2<real>(real(x), real(y));
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	auto vec = v.to<decltype(this->context.matrix.sx)>();
	this->context.matrix.transform_2x2(&vec.x(), &vec.y());
	return vec.to<real>();
#endif
}

r4::rectangle<real> canvas::get_shape_bounding_box()const{
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	double x1, y1, x2, y2;

	// According to SVG spec https://www.w3.org/TR/SVG/coords.html#ObjectBoundingBox
	// "The bounding box is computed exclusive of any values for clipping, masking, filter effects, opacity and stroke-width"
	cairo_path_extents(
			this->cr,
			&x1,
			&y1,
			&x2,
			&y2
		);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)

	return r4::rectangle<real>{
			{real(x1), real(y1)},
			{real(x2 - x1), real(y2 - y1)}
		};
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	// TODO:
	return {0, 0};
#endif
}

bool canvas::has_current_point()const{
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	auto cp = cairo_has_current_point(this->cr);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
	return bool(cp != 0);
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	return this->context.path.total_vertices() != 0;
#endif
}

r4::vector2<real> canvas::get_current_point()const{
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	auto has_cur_p = cairo_has_current_point(this->cr);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
	if(has_cur_p){
		double xx, yy;
		cairo_get_current_point(this->cr, &xx, &yy);
		ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
		return {real(xx), real(yy)};
	}
	cairo_move_to(this->cr, 0, 0);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
	return real(0);
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	return {
			real(this->context.path.last_x()),
			real(this->context.path.last_y())
		};
#endif
}

void canvas::move_to_abs(const r4::vector2<real>& p){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_move_to(this->cr, double(p.x()), double(p.y()));
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	this->context.path.move_to(p.x(), p.y());
#endif
}

void canvas::move_to_rel(const r4::vector2<real>& p){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_rel_move_to(this->cr, double(p.x()), double(p.y()));
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	this->context.path.move_rel(p.x(), p.y());
#endif
}

void canvas::line_to_abs(const r4::vector2<real>& p){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_line_to(this->cr, double(p.x()), double(p.y()));
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	this->context.path.line_to(p.x(), p.y());
#endif
}

void canvas::line_to_rel(const r4::vector2<real>& p){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_rel_line_to(this->cr, double(p.x()), double(p.y()));
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	this->context.path.line_rel(p.x(), p.y());
#endif
}

void canvas::quadratic_curve_to_abs(const r4::vector2<real>& cp1, const r4::vector2<real>& ep){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	double x0, y0; // current point, absolute coordinates
	if (cairo_has_current_point(this->cr)) {
		ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
		cairo_get_current_point(this->cr, &x0, &y0);
		ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
	}
	else {
		ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
		cairo_move_to(this->cr, 0, 0);
		ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
		x0 = 0;
		y0 = 0;
	}
	cairo_curve_to(
			this->cr,
			2.0 / 3.0 * double(cp1.x()) + 1.0 / 3.0 * x0,
			2.0 / 3.0 * double(cp1.y()) + 1.0 / 3.0 * y0,
			2.0 / 3.0 * double(cp1.x()) + 1.0 / 3.0 * double(ep.x()),
			2.0 / 3.0 * double(cp1.y()) + 1.0 / 3.0 * double(ep.y()),
			double(ep.x()),
			double(ep.y())
		);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	agg::curve3 curve;
	curve.approximation_method(agg::curve_approximation_method_e::curve_div);
	curve.approximation_scale(1);
	curve.angle_tolerance(agg::deg2rad(22));
	curve.cusp_limit(agg::deg2rad(0));
	curve.init(
			this->context.path.last_x(), this->context.path.last_y(),
			cp1.x(), cp1.y(),
			ep.x(), ep.y()
		);

	this->context.path.join_path(curve);
#endif
}

void canvas::quadratic_curve_to_rel(const r4::vector2<real>& cp1, const r4::vector2<real>& ep){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_rel_curve_to(
			this->cr,
			2.0 / 3.0 * double(cp1.x()),
			2.0 / 3.0 * double(cp1.y()),
			2.0 / 3.0 * double(cp1.x()) + 1.0 / 3.0 * double(ep.x()),
			2.0 / 3.0 * double(cp1.y()) + 1.0 / 3.0 * double(ep.y()),
			double(ep.x()),
			double(ep.y())
		);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	r4::vector2<real> d{
			real(this->context.path.last_x()),
			real(this->context.path.last_y())
		};
	this->quadratic_curve_to_abs(cp1 + d, ep + d);
#endif
}

void canvas::cubic_curve_to_abs(const r4::vector2<real>& cp1, const r4::vector2<real>& cp2, const r4::vector2<real>& ep){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_curve_to(
			this->cr,
			double(cp1.x()),
			double(cp1.y()),
			double(cp2.x()),
			double(cp2.y()),
			double(ep.x()),
			double(ep.y())
		);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	agg::curve4 curve;
	curve.approximation_method(agg::curve_approximation_method_e::curve_div);
	curve.approximation_scale(1);
	curve.angle_tolerance(agg::deg2rad(22));
	curve.cusp_limit(agg::deg2rad(0));
	curve.init(
			this->context.path.last_x(), this->context.path.last_y(),
			cp1.x(), cp1.y(),
			cp2.x(), cp2.y(),
			ep.x(), ep.y()
		);

	this->context.path.join_path(curve);
#endif
}

void canvas::cubic_curve_to_rel(const r4::vector2<real>& cp1, const r4::vector2<real>& cp2, const r4::vector2<real>& ep){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_rel_curve_to(
			this->cr,
			double(cp1.x()),
			double(cp1.y()),
			double(cp2.x()),
			double(cp2.y()),
			double(ep.x()),
			double(ep.y())
		);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	r4::vector2<real> d{
			real(this->context.path.last_x()),
			real(this->context.path.last_y())
		};

	this->cubic_curve_to_abs(cp1 + d, cp2 + d, ep + d);
#endif
}

void canvas::close_path(){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_close_path(this->cr);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	this->context.path.close_polygon();
#endif
}

void canvas::clear_path(){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_new_path(this->cr);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	this->context.path.remove_all();
#endif
}

void canvas::arc_abs(const r4::vector2<real>& center, real radius, real angle1, real angle2){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	if(angle2 > angle1){
		cairo_arc(this->cr, double(center.x()), double(center.y()), double(radius), double(angle1), double(angle2));
	}else{
		cairo_arc_negative(this->cr, double(center.x()), double(center.y()), double(radius), double(angle1), double(angle2));
	}
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	// TODO:
#endif
}

void canvas::fill(){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_fill_preserve(this->cr);
	ASSERT_INFO(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS, "cairo error: " << cairo_status_to_string(cairo_status(this->cr)))
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	agg::conv_transform<agg::path_storage, agg::trans_affine> transformed_path(this->context.path, this->context.matrix);

	this->rasterizer.add_path(transformed_path);

    this->renderer.color(this->context.color);
    agg::render_scanlines(this->rasterizer, this->scanline, this->renderer);
#endif
}

void canvas::stroke(){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_stroke_preserve(this->cr);
	ASSERT_INFO(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS, "cairo error: " << cairo_status_to_string(cairo_status(this->cr)))
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	agg::conv_stroke<decltype(this->context.path)> stroke_path(this->context.path);
	stroke_path.width(this->context.line_width);
	// stroke_path.line_join(agg::line_join_e(m_line_join.cur_item()));
	// stroke_path.line_cap(agg::line_cap_e(m_line_cap.cur_item()));
	// stroke_path.inner_join(agg::inner_join_e(m_inner_join.cur_item()));
	// stroke_path.inner_miter_limit(1.01);

	agg::conv_transform<decltype(stroke_path), agg::trans_affine> transformed_path(stroke_path, this->context.matrix);

	this->rasterizer.add_path(transformed_path);

    this->renderer.color(this->context.color);
    agg::render_scanlines(this->rasterizer, this->scanline, this->renderer);
#endif
}

void canvas::rectangle(const r4::rectangle<real>& rect){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_rectangle(this->cr, double(rect.p.x()), double(rect.p.y()), double(rect.d.x()), double(rect.d.y()));
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#endif
}

void canvas::set_line_width(real width){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_set_line_width(this->cr, double(width));
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	this->context.line_width = width;
#endif
}

void canvas::set_line_cap(svgdom::stroke_line_cap lc){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_line_cap_t clc;
	switch(lc){
		default:
		case svgdom::stroke_line_cap::butt:
			clc = CAIRO_LINE_CAP_BUTT;
			break;
		case svgdom::stroke_line_cap::round:
			clc = CAIRO_LINE_CAP_ROUND;
			break;
		case svgdom::stroke_line_cap::square:
			clc = CAIRO_LINE_CAP_SQUARE;
			break;
	}
	cairo_set_line_cap(this->cr, clc);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#endif
}

void canvas::set_line_join(svgdom::stroke_line_join lj){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_line_join_t clj;
	switch(lj){
		default:
		case svgdom::stroke_line_join::miter:
			clj = CAIRO_LINE_JOIN_MITER;
			break;
		case svgdom::stroke_line_join::round:
			clj = CAIRO_LINE_JOIN_ROUND;
			break;
		case svgdom::stroke_line_join::bevel:
			clj = CAIRO_LINE_JOIN_BEVEL;
			break;
	}
	cairo_set_line_join(this->cr, clj);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#endif
}

void canvas::push_context(){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_save(this->cr);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	this->context_stack.push_back(this->context);
#endif
}

void canvas::pop_context(){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_restore(this->cr);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	if(this->context_stack.empty()){
		throw std::logic_error("canvas::pop_contex(): context stack is empty");
	}
	this->context = this->context_stack.back();
	this->context_stack.pop_back();
#endif
}

r4::matrix2<real> canvas::get_matrix(){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_matrix_t cm;
	cairo_get_matrix(this->cr, &cm);
	ASSERT(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS)
	return {
		{real(cm.xx), real(cm.xy), real(cm.x0)},
		{real(cm.yx), real(cm.yy), real(cm.y0)}
	};
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	return to_r4_matrix(this->context.matrix);
#endif
}

void canvas::set_matrix(const r4::matrix2<real>& m){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_matrix_t cm;
	cm.xx = double(m[0][0]); cm.xy = double(m[0][1]); cm.x0 = double(m[0][2]);
	cm.yx = double(m[1][0]); cm.yy = double(m[1][1]); cm.y0 = double(m[1][2]);
	cairo_set_matrix(this->cr, &cm);
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	this->context.matrix = to_agg_matrix(m);
#endif
}

svgren::surface canvas::get_sub_surface(const r4::rectangle<unsigned>& region){
	r4::vector2<unsigned> dims;
	uint32_t* buffer;
	unsigned stride;

#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	{
		auto s = cairo_get_group_target(cr);
		ASSERT(s)
		
		stride = cairo_image_surface_get_stride(s); // stride is returned in bytes
		
		dims = decltype(dims){
			unsigned(cairo_image_surface_get_width(s)),
			unsigned(cairo_image_surface_get_height(s))
		};

		buffer = reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(s));
	}
#elif SVGREN_BACKEND == SVGREN_BACKEND_AGG
	dims = decltype(dims){this->rendering_buffer.width(), this->rendering_buffer.height()};
	stride = this->rendering_buffer.stride_abs();
	buffer = reinterpret_cast<uint32_t*>(this->rendering_buffer.buf());
#endif

	ASSERT(buffer)
	ASSERT(stride % sizeof(uint32_t) == 0)

	svgren::surface ret;
	ret.stride = stride / sizeof(uint32_t);

	using std::min;
	ret.d = min(region.d, dims - region.p);
	ret.span = utki::make_span(
			buffer + region.p.y() * ret.stride + region.p.x(),
			ret.stride * ret.d.y() - (ret.stride - ret.d.x()) // subtract 'tail' from last pixels row
		);
	ret.p = region.p;
	
	ASSERT(ret.d.y() <= dims.y())
	ASSERT(ret.d.y() == 0 || std::next(ret.span.begin(), ret.stride * (ret.d.y() - 1)) < ret.span.end())

	return ret;
}

void canvas::push_group(){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_push_group(this->cr);
	if(cairo_status(this->cr) != CAIRO_STATUS_SUCCESS){
		throw std::runtime_error("cairo_push_group() failed");
	}
#endif
}

void canvas::pop_group(real opacity){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	cairo_pop_group_to_source(this->cr);
	ASSERT_INFO(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS, "cairo error: " << cairo_status_to_string(cairo_status(this->cr)))

	if(opacity < 1){
		cairo_paint_with_alpha(this->cr, opacity);
	}else{
		cairo_paint(this->cr);
	}
	ASSERT_INFO(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS, "cairo error: " << cairo_status_to_string(cairo_status(this->cr)))
#endif
}

void canvas::pop_mask_and_group(){
#if SVGREN_BACKEND == SVGREN_BACKEND_CAIRO
	this->get_sub_surface().append_luminance_to_alpha();

	cairo_pattern_t* mask = cairo_pop_group(this->cr);
	ASSERT_INFO(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS, "cairo error: " << cairo_status_to_string(cairo_status(this->cr)))

	utki::scope_exit scope_exit([mask](){
		cairo_pattern_destroy(mask);
	});

	cairo_pop_group_to_source(this->cr);
	ASSERT_INFO(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS, "cairo error: " << cairo_status_to_string(cairo_status(this->cr)))

	cairo_mask(this->cr, mask);
	ASSERT_INFO(cairo_status(this->cr) == CAIRO_STATUS_SUCCESS, "cairo error: " << cairo_status_to_string(cairo_status(this->cr)))
#endif
}
