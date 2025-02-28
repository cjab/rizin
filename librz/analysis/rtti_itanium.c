// SPDX-FileCopyrightText: 2018 pancake <pancake@nopcode.org>
// SPDX-FileCopyrightText: 2018 r00tus3r <iamakshayajayan@gmail.com>
// SPDX-License-Identifier: LGPL-3.0-only

#include <rz_analysis.h>
#include <rz_core.h>
#include <rz_flag.h>
#include <rz_cons.h>
#include <rz_cmd.h>

#define VMI_CLASS_TYPE_INFO_NAME "__vmi_class_type_info"
#define SI_CLASS_TYPE_INFO_NAME  "__si_class_type_info"
#define CLASS_TYPE_INFO_NAME     "__class_type_info"
#define NAME_BUF_SIZE            256

#define VT_WORD_SIZE(ctx) \
	(ctx->word_size)

typedef enum {
	RZ_TYPEINFO_TYPE_UNKNOWN,
	RZ_TYPEINFO_TYPE_CLASS,
	RZ_TYPEINFO_TYPE_SI_CLASS,
	RZ_TYPEINFO_TYPE_VMI_CLASS
} RTypeInfoType;

typedef struct class_type_info_t {
	RTypeInfoType type;
	ut64 class_vtable_addr;
	ut64 typeinfo_addr;
	ut64 vtable_addr;
	ut64 name_addr;
	char *name;
	bool name_unique;
} class_type_info;

typedef struct base_class_type_info_t {
	ut64 base_class_addr;
	ut64 flags;
	enum flags_masks_e {
		base_is_virtual = 0x1,
		base_is_public = 0x2
	} flags_masks;
} base_class_type_info;

typedef struct si_class_type_info_t {
	RTypeInfoType type;
	ut64 class_vtable_addr;
	ut64 typeinfo_addr;
	ut64 vtable_addr;
	ut64 name_addr;
	char *name;
	bool name_unique;
	ut64 base_class_addr;
} si_class_type_info;

typedef struct vmi_class_type_info_t {
	RTypeInfoType type;
	ut64 class_vtable_addr;
	ut64 typeinfo_addr;
	ut64 vtable_addr;
	ut64 name_addr;
	char *name;
	bool name_unique;
	int vmi_flags;
	int vmi_base_count;
	base_class_type_info *vmi_bases;
	enum vmi_flags_masks_e {
		non_diamond_repeat_mask = 0x1,
		diamond_shaped_mask = 0x2,
		non_public_base_mask = 0x4,
		public_base_mask = 0x8
	} vmi_flags_masks;
} vmi_class_type_info;

static bool rtti_itanium_read_type_name(RVTableContext *context, ut64 addr, class_type_info *cti) {
	ut64 at;
	if (!context->read_addr(context->analysis, addr, &at)) {
		return false;
	}
	ut64 unique_mask = 1ULL << (VT_WORD_SIZE(context) * 8 - 1);
	cti->name_unique = (at & unique_mask) == 0;
	at &= ~unique_mask;
	cti->name_addr = at;
	ut8 buf[NAME_BUF_SIZE];
	if (!context->analysis->iob.read_at(context->analysis->iob.io, at, buf, sizeof(buf))) {
		return false;
	}
	buf[NAME_BUF_SIZE - 1] = 0;
	cti->name = rz_analysis_rtti_itanium_demangle_class_name(context, (char *)buf);
	if (!cti->name) {
		return false;
	}
	return true;
}

// Custom for the prototype now
static char *rtti_itanium_read_type_name_custom(RVTableContext *context, ut64 addr, ut64 *str_addr, bool *unique_name) {
	ut64 at;
	if (!context->read_addr(context->analysis, addr, &at)) {
		return NULL;
	}
	ut64 unique_mask = 1ULL << (VT_WORD_SIZE(context) * 8 - 1);
	*unique_name = (at & unique_mask) == 0;
	at &= ~unique_mask;
	*str_addr = at;
	ut8 buf[NAME_BUF_SIZE];
	if (!context->analysis->iob.read_at(context->analysis->iob.io, at, buf, sizeof(buf))) {
		return NULL;
	}
	buf[NAME_BUF_SIZE - 1] = 0;
	char *name = rz_analysis_rtti_itanium_demangle_class_name(context, (char *)buf);

	return name;
}

static void rtti_itanium_class_type_info_fini(class_type_info *cti) {
	if (cti) {
		free(cti->name);
	}
}

static void rtti_itanium_class_type_info_free(class_type_info *cti) {
	if (cti == NULL) {
		return;
	}

	rtti_itanium_class_type_info_fini(cti);
	free(cti);
}

static bool rtti_itanium_class_type_info_init(RVTableContext *context, ut64 addr, class_type_info *cti) {
	cti->type = RZ_TYPEINFO_TYPE_CLASS;
	ut64 at;
	if (addr == UT64_MAX) {
		return false;
	}
	if (!context->read_addr(context->analysis, addr, &at)) {
		return false;
	}
	cti->vtable_addr = at;
	return rtti_itanium_read_type_name(context, addr + VT_WORD_SIZE(context), cti);
}

static class_type_info *rtti_itanium_class_type_info_new(RVTableContext *context, ut64 addr, ut64 source_vtable) {
	class_type_info *result = RZ_NEW0(class_type_info);
	if (!result) {
		return NULL;
	}

	if (!rtti_itanium_class_type_info_init(context, addr, result)) {
		rtti_itanium_class_type_info_free(result);
		return NULL;
	}

	result->class_vtable_addr = source_vtable;
	result->typeinfo_addr = addr;

	return result;
}

static void rtti_itanium_vmi_class_type_info_fini(vmi_class_type_info *vmi_cti) {
	if (vmi_cti) {
		free(vmi_cti->vmi_bases);
		free(vmi_cti->name);
	}
}

static void rtti_itanium_vmi_class_type_info_free(vmi_class_type_info *cti) {
	if (cti == NULL) {
		return;
	}

	rtti_itanium_vmi_class_type_info_fini(cti);
	free(cti);
}

static bool rtti_itanium_vmi_class_type_info_init(RVTableContext *context, ut64 addr, vmi_class_type_info *vmi_cti) {
	vmi_cti->type = RZ_TYPEINFO_TYPE_VMI_CLASS;
	ut64 at;
	if (addr == UT64_MAX) {
		return false;
	}
	if (!context->read_addr(context->analysis, addr, &at)) {
		return false;
	}
	vmi_cti->vtable_addr = at;
	addr += VT_WORD_SIZE(context);
	if (!rtti_itanium_read_type_name(context, addr, (class_type_info *)vmi_cti)) {
		return false;
	}
	addr += VT_WORD_SIZE(context);
	if (!context->read_addr(context->analysis, addr, &at)) {
		return false;
	}
	vmi_cti->vmi_flags = at & 0xffffffff;
	addr += 0x4;
	if (!context->read_addr(context->analysis, addr, &at)) {
		return false;
	}
	at = at & 0xffffffff;
	if (at < 1 || at > 0xfffff) {
		RZ_LOG_ERROR("Cannot read vmi_base_count\n");
		return false;
	}
	vmi_cti->vmi_base_count = at;
	vmi_cti->vmi_bases = calloc(sizeof(base_class_type_info), vmi_cti->vmi_base_count);
	if (!vmi_cti->vmi_bases) {
		return false;
	}
	ut64 tmp_addr = addr + 0x4;

	int i;
	for (i = 0; i < vmi_cti->vmi_base_count; i++) {
		if (!context->read_addr(context->analysis, tmp_addr, &at)) {
			return false;
		}
		vmi_cti->vmi_bases[i].base_class_addr = at;
		tmp_addr += VT_WORD_SIZE(context);
		if (!context->read_addr(context->analysis, tmp_addr, &at)) {
			return false;
		}
		vmi_cti->vmi_bases[i].flags = at;
		tmp_addr += VT_WORD_SIZE(context);
	}
	return true;
}

static vmi_class_type_info *rtti_itanium_vmi_class_type_info_new(RVTableContext *context, ut64 addr, ut64 source_vtable) {
	vmi_class_type_info *result = RZ_NEW0(vmi_class_type_info);
	if (!result) {
		return NULL;
	}

	if (!rtti_itanium_vmi_class_type_info_init(context, addr, result)) {
		rtti_itanium_vmi_class_type_info_free(result);
		return NULL;
	}

	result->class_vtable_addr = source_vtable;
	result->typeinfo_addr = addr;

	return result;
}

static void rtti_itanium_si_class_type_info_fini(si_class_type_info *si_cti) {
	if (si_cti) {
		free(si_cti->name);
	}
}

static void rtti_itanium_si_class_type_info_free(si_class_type_info *cti) {
	if (cti == NULL) {
		return;
	}

	rtti_itanium_si_class_type_info_fini(cti);
	free(cti);
}

static bool rtti_itanium_si_class_type_info_init(RVTableContext *context, ut64 addr, si_class_type_info *si_cti) {
	si_cti->type = RZ_TYPEINFO_TYPE_SI_CLASS;
	ut64 at;
	if (addr == UT64_MAX) {
		return false;
	}
	if (!context->read_addr(context->analysis, addr, &at)) {
		return false;
	}
	si_cti->vtable_addr = at;
	if (!rtti_itanium_read_type_name(context, addr + VT_WORD_SIZE(context), (class_type_info *)si_cti)) {
		return false;
	}
	if (!context->read_addr(context->analysis, addr + 2 * VT_WORD_SIZE(context), &at)) {
		return false;
	}
	si_cti->base_class_addr = at;
	return true;
}

static si_class_type_info *rtti_itanium_si_class_type_info_new(RVTableContext *context, ut64 addr, ut64 source_vtable) {
	si_class_type_info *result = RZ_NEW0(si_class_type_info);
	if (!result) {
		return NULL;
	}

	if (!rtti_itanium_si_class_type_info_init(context, addr, result)) {
		rtti_itanium_si_class_type_info_free(result);
		return NULL;
	}

	result->class_vtable_addr = source_vtable;
	result->typeinfo_addr = addr;

	return result;
}

static const char *type_to_string(RTypeInfoType type) {
	switch (type) {
	case RZ_TYPEINFO_TYPE_CLASS:
		return CLASS_TYPE_INFO_NAME;
	case RZ_TYPEINFO_TYPE_SI_CLASS:
		return SI_CLASS_TYPE_INFO_NAME;
	case RZ_TYPEINFO_TYPE_VMI_CLASS:
		return VMI_CLASS_TYPE_INFO_NAME;
	default:
		rz_return_val_if_reached(CLASS_TYPE_INFO_NAME);
	}
}
static void rtti_itanium_print_class_type_info(class_type_info *cti, const char *prefix) {
	rz_cons_printf("%sType Info at 0x%08" PFMT64x ":\n"
		       "%s  Type Info type: %s\n"
		       "%s  Belongs to class vtable: 0x%08" PFMT64x "\n"
		       "%s  Reference to RTTI's type class: 0x%08" PFMT64x "\n"
		       "%s  Reference to type's name: 0x%08" PFMT64x "\n"
		       "%s  Type Name: %s\n"
		       "%s  Name unique: %s\n",
		prefix, cti->typeinfo_addr,
		prefix, type_to_string(cti->type),
		prefix, cti->class_vtable_addr,
		prefix, cti->vtable_addr,
		prefix, cti->name_addr,
		prefix, cti->name,
		prefix, cti->name_unique ? "true" : "false");
}

static void rtti_itanium_print_class_type_info_json(class_type_info *cti) {
	PJ *pj = pj_new();
	if (!pj) {
		return;
	}

	pj_o(pj);
	pj_ks(pj, "type", type_to_string(cti->type));
	pj_kn(pj, "found_at", cti->typeinfo_addr);
	pj_kn(pj, "class_vtable", cti->class_vtable_addr);
	pj_kn(pj, "ref_to_type_class", cti->vtable_addr);
	pj_kn(pj, "ref_to_type_name", cti->name_addr);
	pj_ks(pj, "name", cti->name);
	pj_kb(pj, "name_unique", cti->name_unique);
	pj_end(pj);

	rz_cons_print(pj_string(pj));
	pj_free(pj);
}

static void rtti_itanium_print_vmi_class_type_info(vmi_class_type_info *vmi_cti, const char *prefix) {
	rz_cons_printf("%sType Info at 0x%08" PFMT64x ":\n"
		       "%s  Type Info type: %s\n"
		       "%s  Belongs to class vtable: 0x%08" PFMT64x "\n"
		       "%s  Reference to RTTI's type class: 0x%08" PFMT64x "\n"
		       "%s  Reference to type's name: 0x%08" PFMT64x "\n"
		       "%s  Type Name: %s\n"
		       "%s  Name unique: %s\n"
		       "%s  Flags: 0x%x\n"
		       "%s  Count of base classes: 0x%x"
		       "\n",
		prefix, vmi_cti->typeinfo_addr,
		prefix, type_to_string(vmi_cti->type),
		prefix, vmi_cti->class_vtable_addr,
		prefix, vmi_cti->vtable_addr,
		prefix, vmi_cti->name_addr,
		prefix, vmi_cti->name,
		prefix, vmi_cti->name_unique ? "true" : "false",
		prefix, vmi_cti->vmi_flags,
		prefix, vmi_cti->vmi_base_count);

	int i;
	for (i = 0; i < vmi_cti->vmi_base_count; i++) {
		rz_cons_printf("%s    Base class type descriptor address: 0x%08" PFMT64x "\n"
			       "%s    Base class flags: 0x%" PFMT64x
			       "\n",
			prefix, vmi_cti->vmi_bases[i].base_class_addr,
			prefix, vmi_cti->vmi_bases[i].flags);
	}
}

static void rtti_itanium_print_vmi_class_type_info_json(vmi_class_type_info *vmi_cti) {
	PJ *pj = pj_new();
	if (!pj) {
		return;
	}

	pj_o(pj);
	pj_ks(pj, "type", type_to_string(vmi_cti->type));
	pj_kn(pj, "found_at", vmi_cti->typeinfo_addr);
	pj_kn(pj, "class_vtable", vmi_cti->class_vtable_addr);
	pj_kn(pj, "ref_to_type_class", vmi_cti->vtable_addr);
	pj_kn(pj, "ref_to_type_name", vmi_cti->name_addr);
	pj_ks(pj, "name", vmi_cti->name);
	pj_kb(pj, "name_unique", vmi_cti->name_unique);
	pj_kn(pj, "flags", vmi_cti->vmi_flags);
	pj_k(pj, "base_classes");
	pj_a(pj);
	int i;
	for (i = 0; i < vmi_cti->vmi_base_count; i++) {
		pj_o(pj);
		pj_kn(pj, "type_desc_addr", vmi_cti->vmi_bases[i].base_class_addr);
		pj_kN(pj, "flags", vmi_cti->vmi_bases[i].flags);
		pj_end(pj);
	}
	pj_end(pj);
	pj_end(pj);

	rz_cons_print(pj_string(pj));
	pj_free(pj);
}

static void rtti_itanium_print_si_class_type_info(si_class_type_info *si_cti, const char *prefix) {
	rz_cons_printf("%sType Info at 0x%08" PFMT64x ":\n"
		       "%s  Type Info type: %s\n"
		       "%s  Belongs to class vtable: 0x%08" PFMT64x "\n"
		       "%s  Reference to RTTI's type class: 0x%08" PFMT64x "\n"
		       "%s  Reference to type's name: 0x%08" PFMT64x "\n"
		       "%s  Type Name: %s\n"
		       "%s  Name unique: %s\n"
		       "%s  Reference to parent's type info: 0x%08" PFMT64x "\n",
		prefix, si_cti->typeinfo_addr,
		prefix, type_to_string(si_cti->type),
		prefix, si_cti->class_vtable_addr,
		prefix, si_cti->vtable_addr,
		prefix, si_cti->name_addr,
		prefix, si_cti->name,
		prefix, si_cti->name_unique ? "true" : "false",
		prefix, si_cti->base_class_addr);
}

static void rtti_itanium_print_si_class_type_info_json(si_class_type_info *si_cti) {
	PJ *pj = pj_new();
	if (!pj) {
		return;
	}

	pj_o(pj);
	pj_ks(pj, "type", type_to_string(si_cti->type));
	pj_kn(pj, "found_at", si_cti->typeinfo_addr);
	pj_kn(pj, "class_vtable", si_cti->class_vtable_addr);
	pj_kn(pj, "ref_to_type_class", si_cti->vtable_addr);
	pj_kn(pj, "ref_to_type_name", si_cti->name_addr);
	pj_ks(pj, "name", si_cti->name);
	pj_kb(pj, "name_unique", si_cti->name_unique);
	pj_kn(pj, "ref_to_parent_type", si_cti->base_class_addr);
	pj_end(pj);

	rz_cons_print(pj_string(pj));
	pj_free(pj);
}

static RTypeInfoType rtti_itanium_type_info_type_from_flag(RVTableContext *context, ut64 atAddress) {
	RzCore *core = context->analysis->coreb.core;
	rz_return_val_if_fail(core, RZ_TYPEINFO_TYPE_CLASS);

	// get the reloc flags
	const RzList *flags = context->analysis->flb.get_list(core->flags, atAddress);
	if (!flags) {
		return RZ_TYPEINFO_TYPE_UNKNOWN;
	}

	RzListIter *iter;
	RzFlagItem *flag;
	rz_list_foreach (flags, iter, flag) {
		if (strstr(flag->name, VMI_CLASS_TYPE_INFO_NAME)) {
			return RZ_TYPEINFO_TYPE_VMI_CLASS;
		} else if (strstr(flag->name, SI_CLASS_TYPE_INFO_NAME)) {
			return RZ_TYPEINFO_TYPE_SI_CLASS;
		} else if (strstr(flag->name, CLASS_TYPE_INFO_NAME)) {
			return RZ_TYPEINFO_TYPE_CLASS;
		}
	}

	return RZ_TYPEINFO_TYPE_UNKNOWN;
}

// used to check if vpointer or RTTI can be in the section
static bool can_section_contain_rtti_vpointer(RzBinSection *section) {
	if (!section) {
		return false;
	}
	if (section->is_data) {
		return true;
	}
	return !strcmp(section->name, ".data.rel.ro") ||
		!strcmp(section->name, ".data.rel.ro.local") ||
		rz_str_endswith(section->name, "__const");
}

static class_type_info *create_class_type(ut64 vtable_addr, char *name, ut64 name_addr, bool unique_name, ut64 typeinfo_addr, ut64 source_vtable) {
	class_type_info *result = RZ_NEW0(class_type_info);
	if (!result) {
		return NULL;
	}
	result->type = RZ_TYPEINFO_TYPE_CLASS;

	result->vtable_addr = vtable_addr;
	result->name_addr = name_addr;
	result->name = name;
	result->name_unique = unique_name;
	result->typeinfo_addr = typeinfo_addr;
	result->class_vtable_addr = source_vtable;
	return result;
}

static si_class_type_info *create_si_class_type(ut64 vtable_addr, char *name, ut64 name_addr, bool unique_name, ut64 basetype_addr, ut64 typeinfo_addr, ut64 source_vtable) {
	si_class_type_info *result = RZ_NEW0(si_class_type_info);
	if (!result) {
		return NULL;
	}
	result->type = RZ_TYPEINFO_TYPE_SI_CLASS;
	result->base_class_addr = basetype_addr;
	result->vtable_addr = vtable_addr;
	result->name_addr = name_addr;
	result->name = name;
	result->name_unique = unique_name;
	result->typeinfo_addr = typeinfo_addr;
	result->class_vtable_addr = source_vtable;
	return result;
}

static vmi_class_type_info *create_vmi_class_type(ut64 vtable_addr, char *name, ut64 name_addr, bool unique_name, ut32 flags, ut32 base_count, base_class_type_info *bases, ut64 typeinfo_addr, ut64 source_vtable) {
	vmi_class_type_info *result = RZ_NEW0(vmi_class_type_info);
	if (!result) {
		return NULL;
	}
	result->type = RZ_TYPEINFO_TYPE_VMI_CLASS;
	result->vmi_bases = bases;
	result->vmi_base_count = base_count;
	result->vmi_flags = flags;
	result->vtable_addr = vtable_addr;
	result->name_addr = name_addr;
	result->name = name;
	result->name_unique = unique_name;
	result->typeinfo_addr = typeinfo_addr;
	result->class_vtable_addr = source_vtable;
	return result;
}

/**
 * @brief Try to parse as much valid looking RTTI as you can
 *
 * @param context
 * @param vtable_addr
 * @param rtti_addr
 * @return class_type_info* NULL if not even default class RTTI could be parsed or error
 */
static class_type_info *raw_rtti_parse(RVTableContext *context, ut64 vtable_addr, ut64 rtti_addr) {
	/*
		rtti_ptr   ----->  |                  vptr                |
				   |--------------------------------------|
				   |               type_name              |
				   |--------------------------------------| --- enough for __class_type_info
				   |  __class_type_info *base_type        |
				   |--------------------------------------| --- enough for __si_class_type_info
				   |              uint flags              | --- must be atleast 16bits, it's 32 bit for 64-bit Itanium ABI
				   |--------------------------------------|
				   |           uint base_count            |
				   |--------------------------------------|
				   |  __base_class_type_info base_info[]  |
				   |--------------------------------------|
				   |---------       ARRAY         --------|
				   |-----__class_type_info *base_type-----|
				   |--------- long __offset_flags --------| ----- enough for __vmi_class_type_info
		*/
	ut64 rtti_vptr = 0;
	ut64 addr = rtti_addr;
	if (!context->read_addr(context->analysis, addr, &rtti_vptr)) {
		return NULL;
	}
	RzBinSection *rtti_section = context->analysis->binb.get_vsect_at(context->analysis->binb.bin, rtti_vptr);
	if (rtti_vptr && !can_section_contain_rtti_vpointer(rtti_section)) {
		;
		;
		; // Right now ignore, seems that some binaries have some weird values inside there....
	}
	addr += VT_WORD_SIZE(context); // Move to the next member

	ut64 name_addr = 0;
	bool name_unique = false;
	char *type_name = rtti_itanium_read_type_name_custom(context, addr, &name_addr, &name_unique);
	if (!type_name) {
		return NULL;
	}

	addr += VT_WORD_SIZE(context); // Move to the next member

	// Right now we already have atleast __class_type_info;

	ut64 base_type_rtti = 0;
	if (!context->read_addr(context->analysis, addr, &base_type_rtti)) {
		return create_class_type(rtti_vptr, type_name, name_addr, name_unique, rtti_addr, vtable_addr);
	}

	RzBinSection *base_type_rtti_section = context->analysis->binb.get_vsect_at(context->analysis->binb.bin, base_type_rtti);
	if (can_section_contain_rtti_vpointer(base_type_rtti_section)) {
		return (class_type_info *)create_si_class_type(rtti_vptr, type_name, name_addr, name_unique, base_type_rtti, rtti_addr, vtable_addr);
	}

	// if it's not a valid base_type_rtti ptr, it might be flags for VMI
	// assume uint are 32bit
	ut64 integers = 0;
	if (!context->read_addr(context->analysis, addr, &integers)) {
		return create_class_type(rtti_vptr, type_name, name_addr, name_unique, rtti_addr, vtable_addr);
	}
	ut32 vmi_flags = integers & 0xffffffff;
	addr += 0x4;
	if (!context->read_addr(context->analysis, addr, &integers)) {
		return create_class_type(rtti_vptr, type_name, name_addr, name_unique, rtti_addr, vtable_addr);
	}
	integers = integers & 0xffffffff;
	if (integers < 1 || integers > 0xfffff) {
		return create_class_type(rtti_vptr, type_name, name_addr, name_unique, rtti_addr, vtable_addr);
	}
	ut32 vmi_base_count = integers;

	base_class_type_info *vmi_bases = calloc(sizeof(base_class_type_info), vmi_base_count);
	if (!vmi_bases) {
		return create_class_type(rtti_vptr, type_name, name_addr, name_unique, rtti_addr, vtable_addr);
	}
	ut64 tmp_addr = addr + 0x4;

	int i;
	for (i = 0; i < vmi_base_count; i++) {
		if (!context->read_addr(context->analysis, tmp_addr, &integers)) {
			free(vmi_bases);
			return create_class_type(rtti_vptr, type_name, name_addr, name_unique, rtti_addr, vtable_addr);
		}
		vmi_bases[i].base_class_addr = integers;
		tmp_addr += VT_WORD_SIZE(context);
		if (!context->read_addr(context->analysis, tmp_addr, &integers)) {
			free(vmi_bases);
			return create_class_type(rtti_vptr, type_name, name_addr, name_unique, rtti_addr, vtable_addr);
		}
		vmi_bases[i].flags = integers;
		tmp_addr += VT_WORD_SIZE(context);
	}
	return (class_type_info *)create_vmi_class_type(rtti_vptr, type_name, name_addr, name_unique, vmi_flags, vmi_base_count, vmi_bases, rtti_addr, vtable_addr);
}

static class_type_info *rtti_itanium_type_info_new(RVTableContext *context, ut64 vtable_addr) {
	/*
		vpointer - 2 words | offset to top |
				   |---------------|
		vpointer - word    | RTTI pointer  |
				   |---------------|
		vpointer   ----->  |  virt_func_0  |
	*/
	ut64 rtti_ptr = vtable_addr - VT_WORD_SIZE(context); // RTTI pointer
	ut64 rtti_addr; // RTTI address

	if (!context->read_addr(context->analysis, rtti_ptr, &rtti_addr)) {
		return NULL;
	}

	RTypeInfoType type = rtti_itanium_type_info_type_from_flag(context, rtti_addr);
	// If there isn't flag telling us the type of TypeInfo
	// try to find the flag in it's vtable
	if (type == RZ_TYPEINFO_TYPE_UNKNOWN) {
		ut64 follow;
		if (!context->read_addr(context->analysis, rtti_addr, &follow)) {
			return NULL;
		}
		follow -= 2 * context->word_size;
		type = rtti_itanium_type_info_type_from_flag(context, follow);
	}

	if (type == RZ_TYPEINFO_TYPE_UNKNOWN) {
		return raw_rtti_parse(context, vtable_addr, rtti_addr);
	}
	switch (type) {
	case RZ_TYPEINFO_TYPE_VMI_CLASS:
		return (class_type_info *)rtti_itanium_vmi_class_type_info_new(context, rtti_addr, vtable_addr);
	case RZ_TYPEINFO_TYPE_SI_CLASS:
		return (class_type_info *)rtti_itanium_si_class_type_info_new(context, rtti_addr, vtable_addr);
	case RZ_TYPEINFO_TYPE_CLASS:
		return rtti_itanium_class_type_info_new(context, rtti_addr, vtable_addr);
	default:
		rz_return_val_if_reached(NULL);
	}
}

static void rtti_itanium_type_info_free(void *info) {
	class_type_info *cti = info;
	if (!cti) {
		return;
	}

	switch (cti->type) {
	case RZ_TYPEINFO_TYPE_VMI_CLASS:
		rtti_itanium_vmi_class_type_info_free((vmi_class_type_info *)cti);
		return;
	case RZ_TYPEINFO_TYPE_SI_CLASS:
		rtti_itanium_si_class_type_info_free((si_class_type_info *)cti);
		return;
	case RZ_TYPEINFO_TYPE_CLASS:
		rtti_itanium_class_type_info_free(cti);
		return;
	default:
		rz_return_if_reached();
	}
}

RZ_API bool rz_analysis_rtti_itanium_print_at_vtable(RVTableContext *context, ut64 addr, RzOutputMode mode) {
	bool use_json = mode == RZ_OUTPUT_MODE_JSON;
	class_type_info *cti = rtti_itanium_type_info_new(context, addr);
	if (!cti) {
		return false;
	}

	switch (cti->type) {
	case RZ_TYPEINFO_TYPE_VMI_CLASS: {
		vmi_class_type_info *vmi_cti = (vmi_class_type_info *)cti;
		if (use_json) {
			rtti_itanium_print_vmi_class_type_info_json(vmi_cti);
		} else {
			rtti_itanium_print_vmi_class_type_info(vmi_cti, "");
		}
		rtti_itanium_vmi_class_type_info_free(vmi_cti);
	}
		return true;
	case RZ_TYPEINFO_TYPE_SI_CLASS: {
		si_class_type_info *si_cti = (si_class_type_info *)cti;
		if (use_json) {
			rtti_itanium_print_si_class_type_info_json(si_cti);
		} else {
			rtti_itanium_print_si_class_type_info(si_cti, "");
		}
		rtti_itanium_si_class_type_info_free(si_cti);
	}
		return true;
	case RZ_TYPEINFO_TYPE_CLASS: {
		if (use_json) {
			rtti_itanium_print_class_type_info_json(cti);
		} else {
			rtti_itanium_print_class_type_info(cti, "");
		}
		rtti_itanium_class_type_info_free(cti);
	}
		return true;
	default:
		rtti_itanium_class_type_info_free(cti);
		rz_return_val_if_reached(false);
	}
}

RZ_API char *rz_analysis_rtti_itanium_demangle_class_name(RVTableContext *context, const char *name) {
	if (!name || !*name) {
		return NULL;
	}

	char *result = NULL;

	if (name[0] != '_') {
		char *to_demangle = rz_str_newf("_Z%s", name);
		result = context->analysis->binb.demangle(NULL, "cxx", to_demangle, 0, false);
		free(to_demangle);
	} else {
		result = context->analysis->binb.demangle(NULL, "cxx", name, 0, false);
	}

	return result;
}

static void recovery_apply_vtable(RVTableContext *context, const char *class_name, RVTableInfo *vtable_info) {
	if (!vtable_info) {
		return;
	}

	ut64 size = rz_analysis_vtable_info_get_size(context, vtable_info);

	RzAnalysisVTable vtable = { .id = NULL, .offset = 0, .size = size, .addr = vtable_info->saddr };
	rz_analysis_class_vtable_set(context->analysis, class_name, &vtable);
	rz_analysis_class_vtable_fini(&vtable);

	RVTableMethodInfo *vmeth;
	rz_vector_foreach(&vtable_info->methods, vmeth) {
		RzAnalysisMethod meth;
		if (!rz_analysis_class_method_exists_by_addr(context->analysis, class_name, vmeth->addr)) {
			meth.addr = vmeth->addr;
			meth.vtable_offset = vmeth->vtable_offset;
			RzAnalysisFunction *fcn = rz_analysis_get_function_at(context->analysis, vmeth->addr);
			meth.name = fcn ? rz_str_new(fcn->name) : rz_str_newf("virtual_%" PFMT64d, meth.vtable_offset);
			// Temporarily set as attr name
			meth.real_name = fcn ? rz_str_new(fcn->name) : rz_str_newf("virtual_%" PFMT64d, meth.vtable_offset);
			meth.method_type = RZ_ANALYSIS_CLASS_METHOD_VIRTUAL;
		} else {
			RzAnalysisMethod exist_meth;
			if (rz_analysis_class_method_get_by_addr(context->analysis, class_name, vmeth->addr, &exist_meth) == RZ_ANALYSIS_CLASS_ERR_SUCCESS) {
				meth.addr = vmeth->addr;
				meth.name = rz_str_new(exist_meth.name);
				meth.real_name = rz_str_new(exist_meth.real_name);
				meth.vtable_offset = vmeth->vtable_offset;
				meth.method_type = RZ_ANALYSIS_CLASS_METHOD_VIRTUAL;
				rz_analysis_class_method_fini(&exist_meth);
			}
		}
		rz_analysis_class_method_set(context->analysis, class_name, &meth);
		rz_analysis_class_method_fini(&meth);
	}
}

/**
 * @brief Add any base class information about the type into analysis/classes
 *
 * @param context
 * @param cti
 */
static void add_class_bases(RVTableContext *context, const class_type_info *cti) {
	class_type_info base_info;
	int i;

	switch (cti->type) {
	case RZ_TYPEINFO_TYPE_SI_CLASS: {
		si_class_type_info *si_class = (void *)cti;
		ut64 base_addr = si_class->base_class_addr;
		base_addr += VT_WORD_SIZE(context); // offset to name
		if (rtti_itanium_read_type_name(context, base_addr, &base_info)) {
			// TODO in future, store the RTTI offset from vtable and use it
			RzAnalysisBaseClass base = { .class_name = base_info.name, .offset = 0 };
			rz_analysis_class_base_set(context->analysis, cti->name, &base);
			rz_analysis_class_base_fini(&base);
		}
	} break;
	case RZ_TYPEINFO_TYPE_VMI_CLASS: {
		vmi_class_type_info *vmi_class = (void *)cti;
		for (i = 0; i < vmi_class->vmi_base_count; i++) {
			base_class_type_info *base_class_info = vmi_class->vmi_bases + i;
			ut64 base_addr = base_class_info->base_class_addr + VT_WORD_SIZE(context); // offset to name
			if (rtti_itanium_read_type_name(context, base_addr, &base_info)) {
				// TODO in future, store the RTTI offset from vtable and use it
				RzAnalysisBaseClass base = { .class_name = base_info.name, .offset = 0 };
				rz_analysis_class_base_set(context->analysis, cti->name, &base);
				rz_analysis_class_base_fini(&base);
			}
		}
	} break;
	default: // other types have no parent classes
		break;
	}
}

static void detect_constructor_destructor(RzAnalysis *analysis, class_type_info *cti) {
	RzVector *vec = rz_analysis_class_method_get_all(analysis, cti->name);
	RzAnalysisMethod *meth;
	rz_vector_foreach(vec, meth) {
		if (!rz_str_cmp(meth->real_name, cti->name, -1)) {
			meth->method_type = RZ_ANALYSIS_CLASS_METHOD_CONSTRUCTOR;
			rz_analysis_class_method_set(analysis, cti->name, meth);
			continue;
		} else if (rz_str_startswith(meth->real_name, "~") && !rz_str_cmp(meth->real_name + 1, cti->name, -1)) {
			if (meth->method_type == RZ_ANALYSIS_CLASS_METHOD_VIRTUAL) {
				meth->method_type = RZ_ANALYSIS_CLASS_METHOD_VIRTUAL_DESTRUCTOR;
			} else {
				meth->method_type = RZ_ANALYSIS_CLASS_METHOD_DESTRUCTOR;
			}
			rz_analysis_class_method_set(analysis, cti->name, meth);
			continue;
		}
	}
	rz_vector_free(vec);
}

RZ_API void rz_analysis_rtti_itanium_recover_all(RVTableContext *context, RzList /*<RVTableInfo *>*/ *vtables) {
	RzList /*<class_type_info>*/ *rtti_list = rz_list_new();
	rtti_list->free = rtti_itanium_type_info_free;
	// to escape multiple same infos from multiple inheritance
	SetU *unique_rttis = set_u_new();

	RzListIter *iter;
	RVTableInfo *vtable;
	rz_list_foreach (vtables, iter, vtable) {
		class_type_info *cti = rtti_itanium_type_info_new(context, vtable->saddr);
		if (!cti) {
			continue;
		}

		rz_analysis_class_create(context->analysis, cti->name);
		// can't we name virtual functions virtual even without RTTI?
		recovery_apply_vtable(context, cti->name, vtable);
		// Temporarily detect by method name
		detect_constructor_destructor(context->analysis, cti);

		// we only need one of a kind
		if (set_u_contains(unique_rttis, cti->typeinfo_addr)) {
			rtti_itanium_type_info_free(cti);
		} else {
			set_u_add(unique_rttis, cti->typeinfo_addr);
			rz_list_append(rtti_list, cti);
		}
	}

	class_type_info *cti;
	rz_list_foreach (rtti_list, iter, cti) {
		add_class_bases(context, cti);
	}

	set_u_free(unique_rttis);
	rz_list_free(rtti_list);
}
