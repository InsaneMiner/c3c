// Copyright (c) 2020 Christoffer Lerno. All rights reserved.
// Use of this source code is governed by a LGPLv3.0
// a copy of which can be found in the LICENSE file.

#include "sema_internal.h"
#include "compiler_internal.h"
#include "bigint.h"


static inline bool sema_resolve_ptr_type(Context *context, TypeInfo *type_info)
{
	if (!sema_resolve_type_shallow(context, type_info->pointer))
	{
		return type_info_poison(type_info);
	}
	type_info->type = type_get_ptr(type_info->pointer->type);
	type_info->resolve_status = RESOLVE_DONE;
	return true;
}


static inline bool sema_resolve_array_type(Context *context, TypeInfo *type)
{
	if (!sema_resolve_type_info(context, type->array.base))
	{
		return type_info_poison(type);
	}
	uint64_t len = 0;
	if (type->array.len)
	{
		if (!sema_analyse_expr_of_required_type(context, type_usize, type->array.len)) return type_info_poison(type);
		if (type->array.len->expr_kind != EXPR_CONST)
		{
			SEMA_ERROR(type->array.len, "Expected a constant value as array size.");
			return type_info_poison(type);
		}
		len = bigint_as_unsigned(&type->array.len->const_expr.i);
	}
	assert(!type->array.len || type->array.len->expr_kind == EXPR_CONST);
	type->type = type_get_array(type->array.base->type, len);
	type->resolve_status = RESOLVE_DONE;
	return true;
}


static bool sema_resolve_type_identifier(Context *context, TypeInfo *type_info)
{
	Decl *ambiguous_decl;
	Decl *decl = sema_resolve_symbol(context,
	                                 type_info->unresolved.name_loc.string,
	                                 type_info->unresolved.path,
	                                 &ambiguous_decl);

	if (!decl)
	{
		SEMA_TOKEN_ERROR(type_info->unresolved.name_loc, "Unknown type '%s'.", type_info->unresolved.name_loc.string);
		return type_info_poison(type_info);
	}

	// Already handled
	if (!decl_ok(decl))
	{
		return type_info_poison(type_info);
	}


	if (ambiguous_decl)
	{
		SEMA_TOKEN_ERROR(type_info->unresolved.name_loc,
		                 "Ambiguous type '%s' – both defined in %s and %s, please add the module name to resolve the ambiguity",
		                 type_info->unresolved.name_loc.string,
		                 decl->module->name->module,
		                 ambiguous_decl->module->name->module);
		return type_info_poison(type_info);
	}
	switch (decl->decl_kind)
	{
		case DECL_THROWS:
			TODO
		case DECL_STRUCT:
		case DECL_UNION:
		case DECL_ERROR:
		case DECL_ENUM:
			type_info->type = decl->type;
			type_info->resolve_status = RESOLVE_DONE;
			DEBUG_LOG("Resolved %s.", type_info->unresolved.name_loc.string);
			return true;
		case DECL_TYPEDEF:
			// TODO func
			if (!sema_resolve_type_info(context, decl->typedef_decl.type_info))
			{
				decl_poison(decl);
				return type_info_poison(type_info);
			}
			DEBUG_LOG("Resolved %s.", type_info->unresolved.name_loc.string);
			type_info->type = decl->typedef_decl.type_info->type;
			type_info->resolve_status = RESOLVE_DONE;
			return true;
		case DECL_POISONED:
			return type_info_poison(type_info);
		case DECL_FUNC:
		case DECL_VAR:
		case DECL_ENUM_CONSTANT:
		case DECL_ERROR_CONSTANT:
		case DECL_ARRAY_VALUE:
		case DECL_IMPORT:
		case DECL_MACRO:
		case DECL_GENERIC:
			SEMA_TOKEN_ERROR(type_info->unresolved.name_loc, "This is not a type.");
			return type_info_poison(type_info);
		case DECL_CT_ELSE:
		case DECL_CT_IF:
		case DECL_CT_ELIF:
		case DECL_ATTRIBUTE:
			UNREACHABLE
	}
	UNREACHABLE

}

bool sema_resolve_type_shallow(Context *context, TypeInfo *type_info)
{
	if (type_info->resolve_status == RESOLVE_DONE) return type_info_ok(type_info);

	if (type_info->resolve_status == RESOLVE_RUNNING)
	{
		// TODO this is incorrect for unresolved expressions
		SEMA_TOKEN_ERROR(type_info->unresolved.name_loc,
		                 "Circular dependency resolving type '%s'.",
		                 type_info->unresolved.name_loc.string);
		return type_info_poison(type_info);
	}

	type_info->resolve_status = RESOLVE_RUNNING;

	switch (type_info->kind)
	{
		case TYPE_INFO_POISON:
		case TYPE_INFO_INC_ARRAY:
			UNREACHABLE
		case TYPE_INFO_IDENTIFIER:
			return sema_resolve_type_identifier(context, type_info);
		case TYPE_INFO_EXPRESSION:
			if (!sema_analyse_expr(context, NULL, type_info->unresolved_type_expr))
			{
				return type_info_poison(type_info);
			}
			TODO
		case TYPE_INFO_ARRAY:
			return sema_resolve_array_type(context, type_info);
		case TYPE_INFO_POINTER:
			return sema_resolve_ptr_type(context, type_info);
	}
	UNREACHABLE
}