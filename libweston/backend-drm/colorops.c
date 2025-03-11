/*
 * Copyright © 2025 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"

#include "color-properties.h"
#include "drm-internal.h"
#include "shared/string-helpers.h"
#include "shared/weston-assert.h"
#include "shared/xalloc.h"

/**
 * Destroys the given colorop 3x1D LUT.
 *
 * @param lut The 3x1D LUT to destroy.
 */
void
drm_colorop_3x1d_lut_destroy(struct drm_colorop_3x1d_lut *lut)
{
	wl_list_remove(&lut->xform_destroy_listener.link);
	wl_list_remove(&lut->link);
	drmModeDestroyPropertyBlob(lut->plane->device->drm.fd, lut->blob_id);
	free(lut);
}

static void
drm_colorop_3x1d_lut_destroy_handler(struct wl_listener *l, void *data)
{
	struct drm_colorop_3x1d_lut *lut =
		wl_container_of(l, lut, xform_destroy_listener);

	drm_colorop_3x1d_lut_destroy(lut);
}

static struct drm_colorop_3x1d_lut *
drm_colorop_3x1d_lut_create(struct drm_plane *plane,
			    struct weston_color_transform *xform,
			    struct weston_color_curve *curve,
			    float *lut, uint32_t len_lut)
{
	struct drm_device *device = plane->device;
	struct drm_colorop_3x1d_lut *colorop_lut;
	struct drm_color_lut *drm_lut;
	uint32_t blob_id;
	unsigned int i;
	int ret;

	drm_lut = xzalloc(len_lut * sizeof(*drm_lut));

	for (i = 0; i < len_lut; i++) {
		drm_lut[i].red   = lut[i] * 0xffff;
		drm_lut[i].green = lut[i + len_lut] * 0xffff;
		drm_lut[i].blue  = lut[i + 2 * len_lut] * 0xffff;
	}

	ret = drmModeCreatePropertyBlob(device->drm.fd, drm_lut,
					len_lut * sizeof(*drm_lut), &blob_id);
	free(drm_lut);

	if (ret < 0)
		return NULL;

	/* Cache the color curve. */
	colorop_lut = xzalloc(sizeof(*colorop_lut));
	colorop_lut->blob_id = blob_id;
	colorop_lut->plane = plane;
	colorop_lut->xform = xform;
	colorop_lut->curve = curve;
	colorop_lut->len = len_lut;
	wl_list_insert(&plane->cached_colorop_3x1d_lut_list, &colorop_lut->link);
	colorop_lut->xform_destroy_listener.notify = drm_colorop_3x1d_lut_destroy_handler;
	wl_signal_add(&xform->destroy_signal, &colorop_lut->xform_destroy_listener);

	return colorop_lut;
}

static struct drm_colorop_3x1d_lut *
drm_colorop_3x1d_lut_from_curve(struct drm_plane *plane,
				struct weston_color_transform *xform,
				enum weston_color_curve_step step,
				uint32_t len_lut)
{
	struct weston_compositor *compositor = xform->cm->compositor;
	struct drm_backend *b = plane->device->backend;
	struct drm_colorop_3x1d_lut *colorop_lut;
	struct weston_color_curve *curve;
	char *err_msg;
	float *lut;

	curve = (step == WESTON_COLOR_CURVE_STEP_PRE) ? &xform->pre_curve :
							&xform->post_curve;

	/* No need, 3x1D LUT already cached. */
	wl_list_for_each(colorop_lut, &plane->cached_colorop_3x1d_lut_list, link)
		if (colorop_lut->curve == curve && colorop_lut->len == len_lut)
			return colorop_lut;

	lut = weston_color_curve_to_3x1D_LUT(compositor, xform, step, len_lut,
					     false, /* forbid bad precision? */
					     &err_msg);
	if (!lut) {
		drm_debug(b, "[colorop] failed to create colorop 3x1D from curve: %s\n",
			     err_msg);
		free(err_msg);
		return NULL;
	}

	colorop_lut = drm_colorop_3x1d_lut_create(plane, xform, curve, lut, len_lut);

	free(lut);

	return colorop_lut;
}

/**
 * Destroys the given colorop color matrix.
 *
 * @param matrix The matrix to destroy.
 */
void
drm_colorop_matrix_destroy(struct drm_colorop_matrix *matrix)
{
	wl_list_remove(&matrix->xform_destroy_listener.link);
	wl_list_remove(&matrix->link);
	drmModeDestroyPropertyBlob(matrix->plane->device->drm.fd, matrix->blob_id);
	free(matrix);
}

static void
drm_colorop_matrix_destroy_handler(struct wl_listener *l, void *data)
{
	struct drm_colorop_matrix *matrix =
		wl_container_of(l, matrix, xform_destroy_listener);

	drm_colorop_matrix_destroy(matrix);
}

/**
 * Float to S31.32 sign-magnitude representation.
 */
static uint64_t
float_to_s31_32_sign_magnitude(float val)
{
	uint64_t ret;

	if (val < 0) {
		ret = (uint64_t) (-val * (1ULL << 32));
		ret |= 1ULL << 63;
	} else {
		ret = (uint64_t) (val * (1ULL << 32));
	}

	return ret;
}

static struct drm_colorop_matrix *
drm_colorop_matrix_create(struct drm_plane *plane,
			  struct weston_color_transform *xform,
			  struct weston_color_mapping *mapping)
{
	struct drm_device *device = plane->device;
	struct drm_colorop_matrix *mat;
	struct drm_color_ctm_3x4 *mat_3x4;
	unsigned int row, col;
	float val;
	uint32_t blob_id;
	int ret;

	/* No need, color matrix already cached. */
	wl_list_for_each(mat, &plane->cached_colorop_matrix_list, link)
		if (mat->mapping == mapping)
			return mat;

	mat_3x4 = xzalloc(sizeof(*mat_3x4));

	/**
	 * mapping->u.mat.matrix is in column-major order. We transpose it and
	 * also add a new column with the offset. Also, kernel requires the
	 * values in S31.32 sign-magnitude representation.
	 *
	 * TODO: maybe simplify this with linalg.h somehow?
	 */
	for (row = 0; row < 3; row++) {
		for (col = 0; col < 3; col++) {
			val = mapping->u.mat.matrix.colmaj[col * 3 + row];
			mat_3x4->matrix[row * 4 + col] = float_to_s31_32_sign_magnitude(val);
		}
		val = mapping->u.mat.offset.el[row];
		mat_3x4->matrix[row * 4 + 3] = float_to_s31_32_sign_magnitude(val);
	}

	ret = drmModeCreatePropertyBlob(device->drm.fd, mat_3x4,
					sizeof(*mat_3x4), &blob_id);
	free(mat_3x4);

	if (ret < 0)
		return NULL;

	/* Cache the matrix. */
	mat = xzalloc(sizeof(*mat));
	mat->blob_id = blob_id;
	mat->plane = plane;
	mat->mapping = mapping;
	wl_list_insert(&plane->cached_colorop_matrix_list, &mat->link);
	mat->xform_destroy_listener.notify = drm_colorop_matrix_destroy_handler;
	wl_signal_add(&xform->destroy_signal, &mat->xform_destroy_listener);

	return mat;
}

/**
 * Given a TF, returns a corresponding enum wdrm_colorop_curve_1d.
 *
 * @param tf_info The transfer function.
 * @param inverse If true, this function tries to find a colorop curve type that
 * corresponds to the mathematical inverse of the TF.
 * @return The corresponding colorop curve type, or WDRM_COLOROP_CURVE_1D__COUNT
 * if there's none.
 */
enum wdrm_colorop_curve_1d
weston_tf_to_colorop_curve(const struct weston_color_tf_info *tf_info, bool inverse)
{
	enum weston_transfer_function tf = tf_info->tf;

	/* TODO: move this to 'color-properties.c'? */

	switch (tf) {
	case WESTON_TF_SRGB:
		/* sRGB piece-wise is the EOTF, and its inverse is the OETF. */
		return inverse ? WDRM_COLOROP_CURVE_1D_SRGB_INV_EOTF :
				 WDRM_COLOROP_CURVE_1D_SRGB_EOTF;
	default:
		return WDRM_COLOROP_CURVE_1D__COUNT;
	}
}

static void
drm_colorop_destroy(struct drm_colorop *colorop)
{
	wl_list_remove(&colorop->link);
	drm_property_info_free(colorop->props, WDRM_COLOROP__COUNT);

	free(colorop);
}

static struct drm_colorop *
drm_colorop_create(struct drm_color_pipeline *pipeline, uint32_t colorop_id,
		   uint32_t *next_colorop_id)
{
	struct drm_device *device = pipeline->plane->device;
	drmModeObjectPropertiesPtr props_drm;
	struct drm_colorop *colorop;

	*next_colorop_id = 0;

	props_drm = drmModeObjectGetProperties(device->drm.fd, colorop_id,
					       DRM_MODE_OBJECT_COLOROP);
	if (!props_drm)
		return NULL;

	colorop = xzalloc(sizeof(*colorop));

	wl_list_insert(pipeline->colorop_list.prev, &colorop->link);

	colorop->id = colorop_id;
	colorop->pipeline = pipeline;

	drm_property_info_populate(device, colorop_props, colorop->props,
				   WDRM_COLOROP__COUNT, props_drm);

	colorop->type = drm_property_get_value(&colorop->props[WDRM_COLOROP_TYPE],
					       props_drm, WDRM_COLOROP_TYPE__COUNT);
	if (colorop->type == WDRM_COLOROP_TYPE__COUNT) {
		drm_colorop_destroy(colorop);
		drmModeFreeObjectProperties(props_drm);
		return NULL;
	}

	colorop->size = drm_property_get_value(&colorop->props[WDRM_COLOROP_SIZE],
					       props_drm, 0);
	if (colorop->size == 0 && (colorop->type == WDRM_COLOROP_TYPE_1D_LUT ||
				   colorop->type == WDRM_COLOROP_TYPE_3D_LUT)) {
		drm_colorop_destroy(colorop);
		drmModeFreeObjectProperties(props_drm);
		return NULL;
	}

	colorop->can_bypass = (colorop->props[WDRM_COLOROP_BYPASS].prop_id != 0);

	*next_colorop_id =
		drm_property_get_value(&colorop->props[WDRM_COLOROP_NEXT],
				       props_drm, 0);

	drmModeFreeObjectProperties(props_drm);

	return colorop;
}

static struct drm_colorop *
drm_colorop_iterate(struct drm_color_pipeline *pipeline, struct drm_colorop *iter)
{
	struct wl_list *list = &pipeline->colorop_list;
	struct wl_list *node;

	if (iter)
		node = iter->link.next;
	else
		node = list->next;

	if (node == list)
		return NULL;

	return container_of(node, struct drm_colorop, link);
}

static char *
drm_color_pipeline_to_str(struct drm_color_pipeline *pipeline)
{
	struct drm_colorop *colorop;
	struct drm_property_info *curve_props;
	const char *type;
	FILE *fp;
	char *str;
	size_t size;
	const char *sep = "	";
	unsigned int i;

	fp = open_memstream(&str, &size);
	abort_oom_if_null(fp);

	fprintf(fp, "[colorop] color pipeline %u (owned by plane %p):\n",
		    pipeline->id, pipeline->plane);

	wl_list_for_each(colorop, &pipeline->colorop_list, link) {
		type = colorop->props[WDRM_COLOROP_TYPE].enum_values[colorop->type].name;

		fprintf(fp, "%s[colorop] id %u, type %s, can bypass? %s",
			    sep, colorop->id, type, yesno(colorop->can_bypass));

		if (colorop->type == WDRM_COLOROP_TYPE_1D_CURVE) {
			curve_props = &colorop->props[WDRM_COLOROP_CURVE_1D];
			for (i = 0; i < curve_props->num_enum_values; i++) {
				if (curve_props->enum_values[i].valid)
					fprintf(fp, " [%s]",
						    curve_props->enum_values[i].name);
			}
		}

		fprintf(fp, "\n");
	}

	fclose(fp);
	return str;
}

static bool
is_colorop_compatible_with_curve(struct weston_compositor *compositor,
				 struct drm_colorop *colorop,
				 struct weston_color_curve *curve)
{
	struct weston_color_curve_parametric param;
	struct drm_property_info *prop_info;
	enum wdrm_colorop_curve_1d curve_type;
	bool inverse;
	bool ret;

	if (colorop->type == WDRM_COLOROP_TYPE_1D_CURVE) {
		if (curve->type != WESTON_COLOR_CURVE_TYPE_ENUM)
			return false;

		inverse = (curve->u.enumerated.tf_direction == WESTON_INVERSE_TF);
		curve_type = weston_tf_to_colorop_curve(curve->u.enumerated.tf,
							inverse);
		if (curve_type == WDRM_COLOROP_CURVE_1D__COUNT)
			return false;

		prop_info = &colorop->props[WDRM_COLOROP_CURVE_1D];
		if (!prop_info->enum_values[curve_type].valid)
			return false;

		return true;
	} else if (colorop->type == WDRM_COLOROP_TYPE_1D_LUT) {
		switch (curve->type) {
		case WESTON_COLOR_CURVE_TYPE_LUT_3x1D:
			return true;
		case WESTON_COLOR_CURVE_TYPE_PARAMETRIC:
			/* Parametric can be lowered to LUT. */
			return true;
		case WESTON_COLOR_CURVE_TYPE_ENUM:
			switch (curve->u.enumerated.tf->tf) {
			case WESTON_TF_ST2084_PQ:
				/* This TF is implemented, so we can lower curve to LUT. */
				return true;
			default:
				/* If we can lower the TF to parametric, we can use it
				 * to create a LUT. */
				ret = weston_color_curve_enum_get_parametric(compositor,
									     &curve->u.enumerated,
									     &param);
				return ret;
			}
		case WESTON_COLOR_CURVE_TYPE_IDENTITY:
			/* Dead code, function never called for IDENTITY. */
			weston_assert_not_reached(compositor,
						  "no need to get colorop for identity curve");
		}

		return false;
	}

	return false;
}

static struct drm_colorop *
search_colorop_compatible_curve(struct drm_color_pipeline *pipeline,
				struct drm_colorop *previous_colorop,
				struct weston_color_curve *curve,
				bool allow_lower_curve)
{
	struct drm_backend *b = pipeline->plane->device->backend;
	struct drm_colorop *colorop = previous_colorop;

	/**
	 * Identity curve should not need a colorop, so calling
	 * this func for IDENTITY is not allowed.
	 */
	weston_assert_uint32_neq(b->compositor,
				 curve->type, WESTON_COLOR_CURVE_TYPE_IDENTITY);

	while ((colorop = drm_colorop_iterate(pipeline, colorop))) {
		switch (curve->type) {
		case WESTON_COLOR_CURVE_TYPE_ENUM:
			if (colorop->type == WDRM_COLOROP_TYPE_1D_CURVE &&
			    is_colorop_compatible_with_curve(b->compositor, colorop, curve))
				return colorop;
			else if (colorop->type == WDRM_COLOROP_TYPE_1D_LUT && allow_lower_curve &&
				 is_colorop_compatible_with_curve(b->compositor, colorop, curve))
				return colorop;
			break;
		case WESTON_COLOR_CURVE_TYPE_PARAMETRIC:
		case WESTON_COLOR_CURVE_TYPE_LUT_3x1D:
			if (colorop->type == WDRM_COLOROP_TYPE_1D_LUT)
				return colorop;
			break;
		case WESTON_COLOR_CURVE_TYPE_IDENTITY:
			/* Dead code. */
			weston_assert_not_reached(b->compositor,
						  "no need to get colorop for identity curve");
		}

		if (!colorop->can_bypass)
			break;
	}

	return NULL;
}

static struct drm_colorop *
search_colorop_type(struct drm_color_pipeline *pipeline,
		    struct drm_colorop *previous_colorop,
		    enum wdrm_colorop_type type)
{
	struct drm_colorop *colorop = previous_colorop;

	while ((colorop = drm_colorop_iterate(pipeline, colorop))) {
		if (colorop->type == type)
			return colorop;

		if (!colorop->can_bypass)
			break;
	}

	return NULL;
}

static struct drm_colorop_state *
drm_colorop_state_create(struct drm_color_pipeline_state *pipeline_state,
			 struct drm_colorop *colorop,
			 struct drm_colorop_state_object object)
{
	struct drm_colorop_state *colorop_state;

	colorop_state = xzalloc(sizeof(*colorop_state));

	wl_list_insert(&pipeline_state->colorop_state_list, &colorop_state->link);

	colorop_state->colorop = colorop;
	colorop_state->object = object;

	return colorop_state;
}

static void
drm_colorop_state_destroy(struct drm_colorop_state *colorop_state)
{
	wl_list_remove(&colorop_state->link);
	free(colorop_state);
}

static struct drm_color_pipeline_state *
drm_color_pipeline_state_create(struct drm_color_pipeline *pipeline,
				struct weston_color_transform *xform)
{
	struct drm_color_pipeline_state *state;

	state = xzalloc(sizeof(*state));

	state->pipeline = pipeline;
	state->xform = xform;

	wl_list_init(&state->colorop_state_list);

	return state;
}

/**
 * Destroys a color pipeline state.
 *
 * @param state The pipeline state to destroy.
 */
void
drm_color_pipeline_state_destroy(struct drm_color_pipeline_state *state)
{
	struct drm_colorop_state *colorop_state, *tmp_colorop_state;

	wl_list_for_each_safe(colorop_state, tmp_colorop_state,
			      &state->colorop_state_list, link)
		drm_colorop_state_destroy(colorop_state);

	free(state);
}

static struct drm_colorop_state *
curve_create_colorop_state(struct drm_color_pipeline_state *pipeline_state,
			   struct drm_colorop *previous_colorop,
			   struct weston_color_transform *xform,
			   enum weston_color_curve_step step,
			   bool allow_lower_curve)
{
	struct drm_color_pipeline *pipeline = pipeline_state->pipeline;
	struct weston_compositor *compositor = pipeline->plane->base.compositor;
	struct weston_color_curve *curve;
	struct drm_colorop_state_object object;
	struct drm_colorop *colorop;
	uint32_t len_lut;

	curve = (step == WESTON_COLOR_CURVE_STEP_PRE) ? &xform->pre_curve :
							&xform->post_curve;

	colorop = search_colorop_compatible_curve(pipeline, previous_colorop,
						  curve, allow_lower_curve);
	if (!colorop)
		return NULL;

	switch (colorop->type) {
	case WDRM_COLOROP_TYPE_1D_CURVE:
		object.type = COLOROP_OBJECT_TYPE_CURVE;
		object.curve = curve;
		break;
	case WDRM_COLOROP_TYPE_1D_LUT:
		len_lut = colorop->size;
		object.type = COLOROP_OBJECT_TYPE_3x1D_LUT;
		object.lut_3x1d = drm_colorop_3x1d_lut_from_curve(pipeline->plane,
								  xform, step, len_lut);
		if (!object.lut_3x1d)
			return NULL;
		break;
	default:
		weston_assert_not_reached(compositor,
					  "curve colorop should be 1D curve or 1D LUT");
	}

	return drm_colorop_state_create(pipeline_state, colorop, object);
}

static struct drm_colorop_state *
mapping_create_colorop_state(struct drm_color_pipeline_state *pipeline_state,
			     struct drm_colorop *previous_colorop,
			     struct weston_color_transform *xform,
			     struct weston_color_mapping *mapping)
{
	struct drm_color_pipeline *pipeline = pipeline_state->pipeline;
	struct weston_compositor *compositor = pipeline->plane->base.compositor;
	struct drm_colorop_state_object object;
	struct drm_colorop *colorop;

	/* For now Weston has only matrices color mapping. */
	weston_assert_uint32_eq(compositor,
				mapping->type, WESTON_COLOR_MAPPING_TYPE_MATRIX);

	colorop = search_colorop_type(pipeline, previous_colorop,
				      WDRM_COLOROP_TYPE_CTM_3X4);
	if (!colorop)
		return NULL;

	object.type = COLOROP_OBJECT_TYPE_MATRIX;
	object.mat = drm_colorop_matrix_create(pipeline->plane, xform,
					       &xform->mapping);
	if (!object.mat)
		return NULL;

	return drm_colorop_state_create(pipeline_state, colorop, object);
}

static struct drm_color_pipeline_state *
drm_color_pipeline_state_from_xform_steps(struct drm_color_pipeline *pipeline,
					  struct weston_color_transform *xform,
					  bool allow_lower_curve,
					  const char *indent)
{
	struct drm_backend *b = pipeline->plane->device->backend;
	struct drm_color_pipeline_state *pipeline_state;
	struct drm_colorop_state *colorop_state;
	struct drm_colorop *previous_colorop;
	uint32_t type;

	pipeline_state = drm_color_pipeline_state_create(pipeline, xform);

	/* First previous_colorop: none. */
	previous_colorop = NULL;

	/* Find colorop for pre-curve. */
	type = xform->pre_curve.type;
	if (type != WESTON_COLOR_CURVE_TYPE_IDENTITY) {
		colorop_state = curve_create_colorop_state(pipeline_state,
							   previous_colorop, xform,
							   WESTON_COLOR_CURVE_STEP_PRE,
							   allow_lower_curve);
		if (!colorop_state)
			goto err;

		previous_colorop = colorop_state->colorop;
	}

	/* Find colorop for color mapping. */
	type = xform->mapping.type;
	if (type != WESTON_COLOR_MAPPING_TYPE_IDENTITY) {
		colorop_state = mapping_create_colorop_state(pipeline_state,
							     previous_colorop,
							     xform, &xform->mapping);
		if (!colorop_state)
			goto err;

		previous_colorop = colorop_state->colorop;
	}

	/* Find colorop for post-curve. */
	type = xform->post_curve.type;
	if (type != WESTON_COLOR_CURVE_TYPE_IDENTITY) {
		colorop_state = curve_create_colorop_state(pipeline_state,
							   previous_colorop, xform,
							   WESTON_COLOR_CURVE_STEP_POST,
							   allow_lower_curve);
		if (!colorop_state)
			goto err;

		previous_colorop = colorop_state->colorop;
	}

	drm_debug(b, "%s[colorop] color pipeline id %u IS compatible with xform %p;\n" \
		     "%s          allow lower curve: %s\n",
		     indent, pipeline->id, xform, indent, yesno(allow_lower_curve));
	return pipeline_state;

err:
	drm_color_pipeline_state_destroy(pipeline_state);
	drm_debug(b, "%s[colorop] color pipeline id %u NOT compatible with xform %p;\n" \
		     "%s          allow lower curve: %s\n",
		     indent, pipeline->id, xform, indent, yesno(allow_lower_curve));
	return NULL;
}

/**
 * Given a color transformation, returns a color pipeline state that can
 * be used to offload such xform to KMS.
 *
 * @param plane The DRM plane that we plan to use to offload the view.
 * @param xform The xform to offload.
 * @param indent To print debug error messages with proper indentation.
 * @return The color pipeline state, or NULL if no color pipelines are
 * compatible with the xform.
 */
struct drm_color_pipeline_state *
drm_color_pipeline_state_from_xform(struct drm_plane *plane,
				    struct weston_color_transform *xform,
				    const char *indent)
{
	struct drm_backend *b = plane->device->backend;
	struct drm_color_pipeline_state *pipeline_state;
	bool bools[2] = {false, true};
	bool allow_lower_curve;
	char *pre_str, *post_str;
	const char *mapping_str;
	unsigned int i, bool_index;

	pre_str = weston_color_curve_type_to_str(&xform->pre_curve);
	post_str = weston_color_curve_type_to_str(&xform->post_curve);
	mapping_str = weston_color_mapping_type_to_str(&xform->mapping);

	drm_debug(b, "%s[colorop] searching color pipeline compatible with xform %p:\n" \
		     "%s          pre %s, mapping %s, post %s\n",
		     indent, xform, indent, pre_str, mapping_str, post_str);
	free(pre_str);
	free(post_str);

	/**
	 * Try to find a compatible pipeline.
	 *
	 * First, we try to find a compatible pipeline but not allowing Weston
	 * enumerated color curves to be lowered to parametric. If we can't find
	 * something, we start allowing that.
	 */
	if (xform->steps_valid) {
		/* TODO: any better hack to loop boolean? */
		for (bool_index = 0; bool_index < ARRAY_LENGTH(bools); bool_index++) {
			allow_lower_curve = bools[bool_index];
			for (i = 0; i < plane->num_color_pipelines; i++) {
				pipeline_state =
					drm_color_pipeline_state_from_xform_steps(&plane->pipelines[i],
										  xform, allow_lower_curve,
										  indent);
				if (pipeline_state)
					return pipeline_state;
			}
		}
	}

	return NULL;
}

/**
 * Populates the color pipelines of a DRM plane.
 *
 * This does nothing if the driver does not support color pipelines.
 *
 * @param plane The DRM plane whose pipelines this populates.
 * @param plane_props The DRM plane's props.
 */
void
drm_plane_populate_color_pipelines(struct drm_plane *plane,
				   drmModeObjectPropertiesPtr plane_props)
{
	struct drm_device *device = plane->device;
	struct drm_backend *b = device->backend;
	drmModePropertyRes *color_pipeline_props;
	char *str;
	uint32_t pipeline_i;
	unsigned int i;

	for (i = 0; i < plane_props->count_props; i++) {
		color_pipeline_props = drmModeGetProperty(device->drm.fd,
							  plane_props->props[i]);
		if (!color_pipeline_props)
			continue;

		if (strcmp(color_pipeline_props->name, "COLOR_PIPELINE") != 0) {
			drmModeFreeProperty(color_pipeline_props);
			continue;
		}

		break;
	}

	if (i == plane_props->count_props)
		return;

	plane->num_color_pipelines = 0;
	for (i = 0; (int)i < color_pipeline_props->count_enums; i++) {
		if (color_pipeline_props->enums[i].value != 0)
			plane->num_color_pipelines++;
	}
	plane->pipelines = xzalloc(plane->num_color_pipelines *
				   sizeof(*plane->pipelines));
	plane->pipeline_props_id = color_pipeline_props->prop_id;

	/* Populate pipelines. */
	pipeline_i = 0;
	for (i = 0; (int)i < color_pipeline_props->count_enums; i++) {
		struct drm_color_pipeline *pipeline;
		struct drm_colorop *colorop;
		uint32_t colorop_id, next_colorop_id;

		/* First colorop. */
		colorop_id = color_pipeline_props->enums[i].value;
		if (colorop_id == 0)
			continue;

		pipeline = &plane->pipelines[pipeline_i++];

		pipeline->plane = plane;
		wl_list_init(&pipeline->colorop_list);

		/* Id of the pipeline is the same of its first colorop. */
		pipeline->id = colorop_id;

		while (colorop_id != 0) {
			colorop = drm_colorop_create(pipeline, colorop_id, &next_colorop_id);
			if (!colorop) {
				drm_debug(b, "[colorop] failed to create colorop for id %u\n", colorop_id);
				drm_plane_release_color_pipelines(plane);
				goto out;
			}
			colorop_id = next_colorop_id;
		}

		if (!wl_list_empty(&pipeline->colorop_list)) {
			str = drm_color_pipeline_to_str(pipeline);
			drm_debug(b, "%s", str);
			free(str);
		}
	}
	weston_assert_uint32_eq(b->compositor,
				plane->num_color_pipelines, pipeline_i);

out:
	drmModeFreeProperty(color_pipeline_props);
}

/**
 * Release the color pipelines of a drm plane.
 *
 * @param plane The drm plane whose pipelines should be released.
 */
void
drm_plane_release_color_pipelines(struct drm_plane *plane)
{
	struct drm_color_pipeline *pipeline;
	struct drm_colorop *colorop, *tmp;
	unsigned int i;

	for (i = 0; i < plane->num_color_pipelines; i++) {
		pipeline = &plane->pipelines[i];
		wl_list_for_each_safe(colorop, tmp, &pipeline->colorop_list, link)
			drm_colorop_destroy(colorop);
	}

	free(plane->pipelines);
	plane->pipelines = NULL;
}
