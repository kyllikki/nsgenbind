/* property generation
 *
 * This file is part of nsgenbind.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2012 Vincent Sanders <vince@netsurf-browser.org>
 */

#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "options.h"
#include "nsgenbind-ast.h"
#include "webidl-ast.h"
#include "jsapi-libdom.h"

static int generate_property_spec(struct binding *binding, const char *interface);
static int generate_property_body(struct binding *binding, const char *interface);


/* search binding for property sharing modifier */
static enum genbind_type_modifier
get_binding_shared_modifier(struct binding *binding, const char *type, const char *ident)
{
	struct genbind_node *shared_node;
	struct genbind_node *shared_mod_node;

	/* look for node matching the ident first */
	shared_node = genbind_node_find_type_ident(binding->binding_list,
				   NULL,
				   GENBIND_NODE_TYPE_BINDING_PROPERTY,
				   ident);

	/* look for a node matching the type */
	if (shared_node == NULL) {
		shared_node = genbind_node_find_type_ident(binding->binding_list,
					   NULL,
					   GENBIND_NODE_TYPE_BINDING_PROPERTY,
					   type);

	}


	if (shared_node != NULL) {
		/* no explicit shared status */
		shared_mod_node = genbind_node_find_type(genbind_node_getnode(shared_node),
						NULL,
						GENBIND_NODE_TYPE_MODIFIER);
		if (shared_mod_node != NULL) {
			return genbind_node_getint(shared_mod_node);
		}
	}
	return GENBIND_TYPE_NONE;
}

static int webidl_property_spec_cb(struct webidl_node *node, void *ctx)
{
	struct binding *binding = ctx;

	struct webidl_node *type_node;
	const char *type = NULL;
	struct webidl_node *ident_node;
	const char *ident;
	struct webidl_node *modifier_node;

	ident_node = webidl_node_find_type(webidl_node_getnode(node),
					   NULL,
					   WEBIDL_NODE_TYPE_IDENT);
	ident = webidl_node_gettext(ident_node);
	if (ident == NULL) {
		/* properties must have an operator
		 * http://www.w3.org/TR/WebIDL/#idl-attributes
		 */
		return -1;
	}


	/* get type name */
	type_node = webidl_node_find_type(webidl_node_getnode(node),
					  NULL,
					  WEBIDL_NODE_TYPE_TYPE);
	ident_node = webidl_node_find_type(webidl_node_getnode(type_node),
					   NULL,
					   WEBIDL_NODE_TYPE_IDENT);
	type = webidl_node_gettext(ident_node);


	/* generate JSAPI_PS macro entry */
	modifier_node = webidl_node_find_type(webidl_node_getnode(node),
					      NULL,
					      WEBIDL_NODE_TYPE_MODIFIER);

	if (webidl_node_getint(modifier_node) == WEBIDL_TYPE_READONLY) {
		fprintf(binding->outfile, "\tJSAPI_PS_RO(\"%s\", ", ident);
	} else {
		fprintf(binding->outfile, "\tJSAPI_PS(\"%s\", ", ident);
	}

	/* generate property shared status */
	switch (get_binding_shared_modifier(binding, type, ident)) {

	default:
	case GENBIND_TYPE_NONE:
		/* shared property without type handler
		 *
		 * js doesnt provide storage and setter/getter must
		 * perform all GC management.
		 */
		fprintf(binding->outfile,
			"%s, 0, JSPROP_ENUMERATE | JSPROP_SHARED",
			ident);
		break;

	case GENBIND_TYPE_TYPE:
		/* shared property with a type handler */
		fprintf(binding->outfile,
			"%s, 0, JSPROP_ENUMERATE | JSPROP_SHARED",
			type);
		break;

	case GENBIND_TYPE_UNSHARED:
		/* unshared property without type handler */
		fprintf(binding->outfile,
			"%s, 0, JSPROP_ENUMERATE",
			ident);
		break;

	case GENBIND_TYPE_TYPE_UNSHARED:
		/* unshared property with a type handler */
		fprintf(binding->outfile,
			"%s, 0, JSPROP_ENUMERATE",
			type);
		break;

	}
	fprintf(binding->outfile, "),\n");

	return 0;
}


/* callback to emit implements property spec */
static int webidl_property_spec_implements_cb(struct webidl_node *node, void *ctx)
{
	struct binding *binding = ctx;

	return generate_property_spec(binding, webidl_node_gettext(node));
}

static int
generate_property_spec(struct binding *binding, const char *interface)
{
	struct webidl_node *interface_node;
	struct webidl_node *members_node;
	struct webidl_node *inherit_node;
	int res = 0;

	/* find interface in webidl with correct ident attached */
	interface_node = webidl_node_find_type_ident(binding->wi_ast,
						     WEBIDL_NODE_TYPE_INTERFACE,
						     interface);

	if (interface_node == NULL) {
		fprintf(stderr,
			"Unable to find interface %s in loaded WebIDL\n",
			interface);
		return -1;
	}

	/* generate property entries for each list (partial interfaces) */
	members_node = webidl_node_find_type(webidl_node_getnode(interface_node),
					NULL,
					WEBIDL_NODE_TYPE_LIST);

	while (members_node != NULL) {

		fprintf(binding->outfile,"\t/**** %s ****/\n", interface);

		/* for each property emit a JSAPI_PS() */
		webidl_node_for_each_type(webidl_node_getnode(members_node),
					  WEBIDL_NODE_TYPE_ATTRIBUTE,
					  webidl_property_spec_cb,
					  binding);

		members_node = webidl_node_find_type(webidl_node_getnode(interface_node),
						members_node,
						WEBIDL_NODE_TYPE_LIST);
	}

	/* check for inherited nodes and insert them too */
	inherit_node = webidl_node_find(webidl_node_getnode(interface_node),
					NULL,
					webidl_cmp_node_type,
					(void *)WEBIDL_NODE_TYPE_INTERFACE_INHERITANCE);

	if (inherit_node != NULL) {
		res = generate_property_spec(binding,
					      webidl_node_gettext(inherit_node));
	}

	if (res == 0) {
		res = webidl_node_for_each_type(webidl_node_getnode(interface_node),
					WEBIDL_NODE_TYPE_INTERFACE_IMPLEMENTS,
					webidl_property_spec_implements_cb,
					binding);
	}

	return res;
}

/* generate property specifier structure */
int
output_property_spec(struct binding *binding)
{
	int res;

	fprintf(binding->outfile,
		"static JSPropertySpec jsclass_properties[] = {\n");

	res = generate_property_spec(binding, binding->interface);

	fprintf(binding->outfile, "\tJSAPI_PS_END\n};\n\n");

	return res;
}

static int output_return(struct binding *binding,
			 const char *ident,
			 struct webidl_node *node)
{
	struct webidl_node *type_node = NULL;
	struct webidl_node *type_base = NULL;
	enum webidl_type webidl_arg_type;

	type_node = webidl_node_find_type(webidl_node_getnode(node),
					 NULL,
					 WEBIDL_NODE_TYPE_TYPE);

	type_base = webidl_node_find_type(webidl_node_getnode(type_node),
					      NULL,
					      WEBIDL_NODE_TYPE_TYPE_BASE);

	webidl_arg_type = webidl_node_getint(type_base);

	switch (webidl_arg_type) {
	case WEBIDL_TYPE_USER:
		/* User type are represented with jsobject */
		fprintf(binding->outfile,
			"\tJSAPI_PROP_SET_RVAL(cx, vp, OBJECT_TO_JSVAL(%s));\n",
			ident);

		break;

	case WEBIDL_TYPE_BOOL:
		/* JSBool */
		fprintf(binding->outfile,
			"\tJSAPI_PROP_SET_RVAL(cx, vp, BOOLEAN_TO_JSVAL(%s));\n",
			ident);

		break;

	case WEBIDL_TYPE_BYTE:
		WARN(WARNING_UNIMPLEMENTED, "Unhandled type WEBIDL_TYPE_BYTE");
		break;

	case WEBIDL_TYPE_OCTET:
		WARN(WARNING_UNIMPLEMENTED, "Unhandled type WEBIDL_TYPE_OCTET");
		break;

	case WEBIDL_TYPE_FLOAT:
	case WEBIDL_TYPE_DOUBLE:
		/* double */
		fprintf(binding->outfile,
			"\tJSAPI_PROP_SET_RVAL(cx, vp, DOUBLE_TO_JSVAL(%s));\n",
			ident);
		break;

	case WEBIDL_TYPE_SHORT:
		/* int16_t  */
		fprintf(binding->outfile,
			"\tJSAPI_PROP_SET_RVAL(cx, vp, INT_TO_JSVAL(%s));\n",
			ident);
		break;

	case WEBIDL_TYPE_LONGLONG:
		WARN(WARNING_UNIMPLEMENTED, "Unhandled type WEBIDL_TYPE_LONGLONG");
		break;

	case WEBIDL_TYPE_LONG:
		/* int32_t  */
		fprintf(binding->outfile,
			"\tJSAPI_PROP_SET_RVAL(cx, vp, INT_TO_JSVAL(%s));\n",
			ident);
		break;

	case WEBIDL_TYPE_STRING:
		/* JSString * */
		fprintf(binding->outfile,
			"\tJSAPI_PROP_SET_RVAL(cx, vp, JSAPI_STRING_TO_JSVAL(%s));\n",
			ident);
		break;

	case WEBIDL_TYPE_SEQUENCE:
		WARN(WARNING_UNIMPLEMENTED, "Unhandled type WEBIDL_TYPE_SEQUENCE");
		break;

	case WEBIDL_TYPE_OBJECT:
		/* JSObject * */
		fprintf(binding->outfile,
			"\tJSAPI_PROP_SET_RVAL(cx, vp, OBJECT_TO_JSVAL(%s));\n",
			ident);
		break;

	case WEBIDL_TYPE_DATE:
		WARN(WARNING_UNIMPLEMENTED, "Unhandled type WEBIDL_TYPE_DATE");
		break;

	case WEBIDL_TYPE_VOID:
		/* specifically requires no value */
		break;

	default:
		break;
	}

	return 0;
}



/* generate variable declaration of the correct type with appropriate default */
static int output_return_declaration(struct binding *binding,
				     const char *ident,
				     struct webidl_node *node)
{
	struct webidl_node *type_node = NULL;
	struct webidl_node *type_base = NULL;
	struct webidl_node *type_name = NULL;
	struct webidl_node *type_mod = NULL;
	enum webidl_type webidl_arg_type;

	type_node = webidl_node_find_type(webidl_node_getnode(node),
					 NULL,
					 WEBIDL_NODE_TYPE_TYPE);

	type_base = webidl_node_find_type(webidl_node_getnode(type_node),
					      NULL,
					      WEBIDL_NODE_TYPE_TYPE_BASE);

	webidl_arg_type = webidl_node_getint(type_base);

	switch (webidl_arg_type) {
	case WEBIDL_TYPE_USER:
		/* User type are represented with jsobject */
		type_name = webidl_node_find_type(webidl_node_getnode(type_node),
						  NULL,
						  WEBIDL_NODE_TYPE_IDENT);
		fprintf(binding->outfile,
			"\tJSObject *%s = NULL; /* %s */\n",
			ident,
			webidl_node_gettext(type_name));

		break;

	case WEBIDL_TYPE_BOOL:
		/* JSBool */
		fprintf(binding->outfile, "\tJSBool %s = JS_FALSE;\n",ident);

		break;

	case WEBIDL_TYPE_BYTE:
		WARN(WARNING_UNIMPLEMENTED, "Unhandled type WEBIDL_TYPE_BYTE");
		break;

	case WEBIDL_TYPE_OCTET:
		WARN(WARNING_UNIMPLEMENTED, "Unhandled type WEBIDL_TYPE_OCTET");
		break;

	case WEBIDL_TYPE_FLOAT:
	case WEBIDL_TYPE_DOUBLE:
		/* double */
		fprintf(binding->outfile, "\tdouble %s = 0;\n",	ident);
		break;

	case WEBIDL_TYPE_SHORT:
		/* int16_t  */
		type_mod = webidl_node_find_type(webidl_node_getnode(type_node),
						 NULL,
						 WEBIDL_NODE_TYPE_MODIFIER);
		if ((type_mod != NULL) &&
		    (webidl_node_getint(type_mod) == WEBIDL_TYPE_MODIFIER_UNSIGNED)) {
			fprintf(binding->outfile,
				"\tuint16_t %s = 0;\n",
				ident);
		} else {
			fprintf(binding->outfile,
				"\tint16_t %s = 0;\n",
				ident);
		}

		break;

	case WEBIDL_TYPE_LONGLONG:
		WARN(WARNING_UNIMPLEMENTED, "Unhandled type WEBIDL_TYPE_LONGLONG");
		break;

	case WEBIDL_TYPE_LONG:
		/* int32_t  */
		type_mod = webidl_node_find_type(webidl_node_getnode(type_node),
						 NULL,
						 WEBIDL_NODE_TYPE_MODIFIER);
		if ((type_mod != NULL) &&
		    (webidl_node_getint(type_mod) == WEBIDL_TYPE_MODIFIER_UNSIGNED)) {
			fprintf(binding->outfile,
				"\tuint32_t %s = 0;\n",
				ident);
		} else {
			fprintf(binding->outfile,
				"\tint32_t %s = 0;\n",
				ident);
		}

		break;

	case WEBIDL_TYPE_STRING:
		/* JSString * */
		fprintf(binding->outfile,
			"\tJSString *%s = NULL;\n",
			ident);
		break;

	case WEBIDL_TYPE_SEQUENCE:
		WARN(WARNING_UNIMPLEMENTED, "Unhandled type WEBIDL_TYPE_SEQUENCE");
		break;

	case WEBIDL_TYPE_OBJECT:
		/* JSObject * */
		fprintf(binding->outfile, "\tJSObject *%s = NULL;\n", ident);
		break;

	case WEBIDL_TYPE_DATE:
		WARN(WARNING_UNIMPLEMENTED, "Unhandled type WEBIDL_TYPE_DATE");
		break;

	case WEBIDL_TYPE_VOID:
		/* specifically requires no value */
		break;

	default:
		break;
	}
	return 0;
}

static int
output_property_placeholder(struct binding *binding,
			    struct webidl_node* oplist,
			    const char *ident)
{
	oplist=oplist;

	WARN(WARNING_UNIMPLEMENTED,
	     "property %s.%s has no implementation\n",
	     binding->interface,
	     ident);

	fprintf(binding->outfile,
		"\tJSLOG(\"property %s.%s has no implementation\");\n",
		binding->interface,
		ident);
	return 0;
}

static int output_property_getter(struct binding *binding,
				  struct webidl_node *node,
				  const char *ident)
{
	struct genbind_node *property_node;

	fprintf(binding->outfile,
		"static JSBool JSAPI_PROP_GETTER(%s, JSContext *cx, JSObject *obj, jsval *vp)\n"
		"{\n",
		ident);

	/* return value declaration */
	output_return_declaration(binding, "jsret", node);

	if (binding->has_private) {
		/* get context */
		fprintf(binding->outfile,
			"\tstruct jsclass_private *private;\n"
			"\n"
			"\tprivate = JS_GetInstancePrivate(cx,\n"
			"\t\tobj,\n"
			"\t\t&JSClass_%s,\n"
			"\t\tNULL);\n"
			"\tif (private == NULL)\n"
			"\t\treturn JS_FALSE;\n\n",
			binding->interface);
	}

	property_node = genbind_node_find_type_ident(binding->gb_ast,
				      NULL,
				      GENBIND_NODE_TYPE_GETTER,
				      ident);

	if (property_node != NULL) {
		/* binding source block */
		output_code_block(binding, genbind_node_getnode(property_node));
	} else {
		/* examine internal variables and see if they are gettable */
		struct genbind_node *binding_node;
		struct genbind_node *internal_node = NULL;

		binding_node = genbind_node_find_type(binding->gb_ast,
						 NULL,
						 GENBIND_NODE_TYPE_BINDING);

		if (binding_node != NULL) {
			internal_node = genbind_node_find_type_ident(genbind_node_getnode(binding_node),
				      NULL,
				      GENBIND_NODE_TYPE_BINDING_INTERNAL,
				      ident);

		}

		if (internal_node != NULL) {
			/** @todo fetching from internal entries ought to be type sensitive */
			fprintf(binding->outfile,
				"\tjsret = private->%s;\n",
				ident);
		} else {
			output_property_placeholder(binding, node, ident);
		}

	}

	output_return(binding, "jsret", node);

	fprintf(binding->outfile,
		"\treturn JS_TRUE;\n"
		"}\n\n");

	return 0;
}

static int output_property_setter(struct binding *binding,
				  struct webidl_node *node,
				  const char *ident)
{
	struct webidl_node *modifier_node;

	modifier_node = webidl_node_find_type(webidl_node_getnode(node),
					      NULL,
					      WEBIDL_NODE_TYPE_MODIFIER);


	if (webidl_node_getint(modifier_node) == WEBIDL_TYPE_READONLY) {
		/* readonly so a set function is not required */
		return 0;
	}


	fprintf(binding->outfile,
		"static JSBool JSAPI_PROP_SETTER(%s, JSContext *cx, JSObject *obj, jsval *vp)\n",
		ident);

	fprintf(binding->outfile,
		"{\n"
		"        return JS_FALSE;\n"
		"}\n\n");


	return 0;
}

static int webidl_property_body_cb(struct webidl_node *node, void *ctx)
{
	struct binding *binding = ctx;
	struct webidl_node *ident_node;
	const char *ident;
	struct webidl_node *type_node;
	const char *type = NULL;
	int ret;
	enum genbind_type_modifier shared_mod;

	ident_node = webidl_node_find_type(webidl_node_getnode(node),
					   NULL,
					   WEBIDL_NODE_TYPE_IDENT);
	ident = webidl_node_gettext(ident_node);
	if (ident == NULL) {
		/* properties must have an operator
		 * http://www.w3.org/TR/WebIDL/#idl-attributes
		 */
		return -1;
	}

	/* get type name */
	type_node = webidl_node_find_type(webidl_node_getnode(node),
					  NULL,
					  WEBIDL_NODE_TYPE_TYPE);
	ident_node = webidl_node_find_type(webidl_node_getnode(type_node),
					   NULL,
					   WEBIDL_NODE_TYPE_IDENT);
	type = webidl_node_gettext(ident_node);

	/* find shared modifiers */
	shared_mod = get_binding_shared_modifier(binding, type, ident);

	/* only generate individual getters/setters if there is not a
	 * type handler
	 */
	if ((shared_mod & GENBIND_TYPE_TYPE) == 0) {
		ret = output_property_setter(binding, node, ident);
		if (ret == 0) {
			/* property getter */
			ret = output_property_getter(binding, node, ident);
		}
	}
	return ret;
}



/* callback to emit implements property bodys */
static int webidl_implements_cb(struct webidl_node *node, void *ctx)
{
	struct binding *binding = ctx;

	return generate_property_body(binding, webidl_node_gettext(node));
}



static int
generate_property_body(struct binding *binding, const char *interface)
{
	struct webidl_node *interface_node;
	struct webidl_node *members_node;
	struct webidl_node *inherit_node;
	int res = 0;

	/* find interface in webidl with correct ident attached */
	interface_node = webidl_node_find_type_ident(binding->wi_ast,
						     WEBIDL_NODE_TYPE_INTERFACE,
						     interface);

	if (interface_node == NULL) {
		fprintf(stderr,
			"Unable to find interface %s in loaded WebIDL\n",
			interface);
		return -1;
	}

	/* generate property bodies */
	members_node = webidl_node_find_type(webidl_node_getnode(interface_node),
					NULL,
					WEBIDL_NODE_TYPE_LIST);
	while (members_node != NULL) {

		fprintf(binding->outfile,"/**** %s ****/\n", interface);

		/* emit property body */
		webidl_node_for_each_type(webidl_node_getnode(members_node),
					  WEBIDL_NODE_TYPE_ATTRIBUTE,
					  webidl_property_body_cb,
					  binding);


		members_node = webidl_node_find_type(webidl_node_getnode(interface_node),
						members_node,
						WEBIDL_NODE_TYPE_LIST);

	}

	/* check for inherited nodes and insert them too */
	inherit_node = webidl_node_find_type(webidl_node_getnode(interface_node),
					NULL,
					WEBIDL_NODE_TYPE_INTERFACE_INHERITANCE);

	if (inherit_node != NULL) {
		res = generate_property_body(binding,
					   webidl_node_gettext(inherit_node));
	}

	if (res == 0) {
		res = webidl_node_for_each_type(webidl_node_getnode(interface_node),
					WEBIDL_NODE_TYPE_INTERFACE_IMPLEMENTS,
					webidl_implements_cb,
					binding);
	}

	return res;
}

	/* setter for type handler */

static int output_property_type_setter(struct binding *binding, struct genbind_node *node, const char *type)
{
	struct genbind_node *property_node;

	fprintf(binding->outfile,
		"static JSBool JSAPI_PROP_SETTER(%s, JSContext *cx, JSObject *obj, jsval *vp)\n"
		"{\n",
		type);

	if (binding->has_private) {
		/* get context */
		fprintf(binding->outfile,
			"\tstruct jsclass_private *private;\n"
			"\n"
			"\tprivate = JS_GetInstancePrivate(cx,\n"
			"\t\tobj,\n"
			"\t\t&JSClass_%s,\n"
			"\t\tNULL);\n"
			"\tif (private == NULL)\n"
			"\t\treturn JS_FALSE;\n\n",
			binding->interface);
	}

	property_node = genbind_node_find_type_ident(binding->gb_ast,
						     NULL,
						     GENBIND_NODE_TYPE_SETTER,
						     type);

	if (property_node != NULL) {
		/* binding source block */
		output_code_block(binding, genbind_node_getnode(property_node));
	}

	fprintf(binding->outfile,
		"        return JS_TRUE;\n"
		"}\n\n");
	return 0;

}


/* getter for type handlers */
static int output_property_type_getter(struct binding *binding, struct genbind_node *node, const char *type)
{
	struct genbind_node *property_node;

	fprintf(binding->outfile,
		"static JSBool JSAPI_PROP_GETTER(%s, JSContext *cx, JSObject *obj, jsval *vp)\n"
		"{\n",
		type);

	if (binding->has_private) {
		/* get context */
		fprintf(binding->outfile,
			"\tstruct jsclass_private *private;\n"
			"\n"
			"\tprivate = JS_GetInstancePrivate(cx,\n"
			"\t\tobj,\n"
			"\t\t&JSClass_%s,\n"
			"\t\tNULL);\n"
			"\tif (private == NULL)\n"
			"\t\treturn JS_FALSE;\n\n",
			binding->interface);
	}

	property_node = genbind_node_find_type_ident(binding->gb_ast,
						     NULL,
						     GENBIND_NODE_TYPE_GETTER,
						     type);

	if (property_node != NULL) {
		/* binding source block */
		output_code_block(binding, genbind_node_getnode(property_node));
	}

	fprintf(binding->outfile,
		"        return JS_TRUE;\n"
		"}\n\n");
	return 0;

}

/* callback to emit property handlers for whole types */
static int typehandler_property_cb(struct genbind_node *node, void *ctx)
{
	struct binding *binding = ctx;
	struct genbind_node *ident_node;
	const char *type;
	struct genbind_node *mod_node;
	enum genbind_type_modifier share_mod;
	int ret = 0;

	mod_node = genbind_node_find_type(genbind_node_getnode(node),
					  NULL,
					  GENBIND_NODE_TYPE_MODIFIER);
	share_mod = genbind_node_getint(mod_node);
	if ((share_mod & GENBIND_TYPE_TYPE) == GENBIND_TYPE_TYPE) {
		/* type handler */

		ident_node = genbind_node_find_type(genbind_node_getnode(node),
						    NULL,
						    GENBIND_NODE_TYPE_IDENT);
		type = genbind_node_gettext(ident_node);
		if (type != NULL) {
			ret = output_property_type_setter(binding, node, type);
			if (ret == 0) {
				/* property getter */
				ret = output_property_type_getter(binding,
								  node,
								  type);
			}
		}
	}
	return ret;
}

/* exported interface documented in jsapi-libdom.h */
int
output_property_body(struct binding *binding)
{
	int res;

	res = generate_property_body(binding, binding->interface);

	if (res == 0) {
		res = genbind_node_for_each_type(binding->binding_list,
					GENBIND_NODE_TYPE_BINDING_PROPERTY,
					typehandler_property_cb,
					binding);
	}

	return res;
}
