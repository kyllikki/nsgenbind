%{
/* parser for the binding generation config file 
 *
 * This file is part of nsgenbind.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2012 Vincent Sanders <vince@netsurf-browser.org>
 */

#include <stdio.h>
#include <string.h>

#define YYFPRINTF genbind_fprintf
#define YY_LOCATION_PRINT(File, Loc)                            \
  genbind_fprintf(File, "%d.%d-%d.%d",                          \
                  (Loc).first_line, (Loc).first_column,         \
                  (Loc).last_line,  (Loc).last_column)

#include "nsgenbind-parser.h"
#include "nsgenbind-lexer.h"
#include "webidl-ast.h"
#include "nsgenbind-ast.h"

char *errtxt;

static void nsgenbind_error(YYLTYPE *locp,
                            struct genbind_node **genbind_ast,
                            const char *str)
{
        locp = locp;
        genbind_ast = genbind_ast;
        errtxt = strdup(str);
}

static struct genbind_node *
add_method(struct genbind_node **genbind_ast,
           long methodtype,
           struct genbind_node *declarator,
           char *cdata)
{
        struct genbind_node *res_node;
        struct genbind_node *method_node;
        struct genbind_node *class_node;
        struct genbind_node *cdata_node;
        char *class_name;

        /* extract the class name from the declarator */
        class_name = genbind_node_gettext(
                genbind_node_find_type(
                        genbind_node_getnode(
                                genbind_node_find_type(
                                        declarator,
                                        NULL,
                                        GENBIND_NODE_TYPE_CLASS)),
                        NULL,
                        GENBIND_NODE_TYPE_IDENT));

        if (cdata == NULL) {
                cdata_node = declarator;
        } else {
                cdata_node = genbind_new_node(GENBIND_NODE_TYPE_CDATA,
                                              declarator,
                                              cdata);
        }

        /* generate method node */
        method_node = genbind_new_node(GENBIND_NODE_TYPE_METHOD,
                                 NULL,
                                 genbind_new_node(GENBIND_NODE_TYPE_METHOD_TYPE,
                                                  cdata_node,
                                                  (void *)methodtype));

        class_node = genbind_node_find_type_ident(*genbind_ast,
                                                  NULL,
                                                  GENBIND_NODE_TYPE_CLASS,
                                                  class_name);
        if (class_node == NULL) {
                /* no existing class so manufacture one and attach method */
                res_node = genbind_new_node(GENBIND_NODE_TYPE_CLASS, NULL,
                                      genbind_new_node(GENBIND_NODE_TYPE_IDENT,
                                                       method_node,
                                                       class_name));
        } else {
                /* update the existing class */

                /* link member node into class_node */
                genbind_node_add(class_node, method_node);

                res_node = NULL; /* updating so no need to add a new node */
        }
        return res_node;
}

%}

%locations
 /* bison prior to 2.4 cannot cope with %define api.pure so we use the
  *  deprecated directive 
  */
%pure-parser
%error-verbose
%parse-param { struct genbind_node **genbind_ast }

%union
{
        char *text;
        struct genbind_node *node;
        long value;
}

%token TOK_BINDING
%token TOK_WEBIDL
%token TOK_PREFACE
%token TOK_PROLOGUE
%token TOK_EPILOGUE
%token TOK_POSTFACE

%token TOK_CLASS
%token TOK_PRIVATE
%token TOK_INTERNAL
%token TOK_FLAGS
%token TOK_TYPE
%token TOK_UNSHARED
%token TOK_SHARED
%token TOK_PROPERTY

 /* method types */
%token TOK_INIT
%token TOK_FINI
%token TOK_METHOD
%token TOK_GETTER
%token TOK_SETTER
%token TOK_PROTOTYPE

%token TOK_DBLCOLON

%token TOK_STRUCT
%token TOK_UNION
%token TOK_UNSIGNED

%token <text> TOK_IDENTIFIER
%token <text> TOK_STRING_LITERAL
%token <text> TOK_CCODE_LITERAL

%type <text> CBlock

%type <value> Modifiers
%type <value> Modifier

%type <node> Statement
%type <node> Statements
%type <node> Binding
%type <node> BindingArgs
%type <node> BindingArg
%type <node> Class
%type <node> ClassArgs
%type <node> ClassArg
%type <node> ClassFlags

%type <node> Method
%type <node> MethodDeclarator
%type <value> MethodType
%type <value> BindingAndMethodType

%type <node> WebIDL

%type <node> ParameterList
%type <node> CTypeIdent
%type <node> CType
%type <node> CTypeSpecifier

%%

Input:
        Statements 
        {
                *genbind_ast = $1;
        }
        ;
        
Statements:
        Statement
        |
        Statements Statement
        {
                $$ = *genbind_ast = genbind_node_prepend($2, $1);
        }
        |
        error ';'
        {
                fprintf(stderr, "%d: %s\n", yylloc.first_line, errtxt);
                free(errtxt);
                YYABORT ;
        }
        ;

Statement:
        Binding
        |
        Class
        |
        Method
        ;

Binding:
        TOK_BINDING TOK_IDENTIFIER '{' BindingArgs '}' ';'
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_BINDING,
                                      NULL,
                                      genbind_new_node(GENBIND_NODE_TYPE_NAME,
                                                       $4, $2));
        }
        ;

BindingArgs:
        BindingArg
        |
        BindingArgs BindingArg
        {
                $$ = genbind_node_link($2, $1);
        }
        ;

BindingArg:
        WebIDL
        |
        BindingAndMethodType CBlock ';'
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_METHOD,
                        NULL,
                        genbind_new_node(GENBIND_NODE_TYPE_METHOD_TYPE,
                                genbind_new_node(GENBIND_NODE_TYPE_CDATA,
                                                 NULL,
                                                 $2),
                                (void *)$1));
        }
        ;

/* a web IDL file specifier */
WebIDL:
        TOK_WEBIDL TOK_STRING_LITERAL ';'
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_WEBIDL, NULL, $2);
        }
        ;

 /* parse a c type specifier. This probably also needs to cope with
  *  void, char, short, int, long, float, double, signed, enum
  */
CTypeSpecifier:
        TOK_UNSIGNED TOK_IDENTIFIER
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_NAME,
                                      genbind_new_node(GENBIND_NODE_TYPE_NAME,
                                                       NULL,
                                                       strdup("unsigned")),
                                      $2);
        }
        |
        TOK_STRUCT TOK_IDENTIFIER
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_NAME,
                                      genbind_new_node(GENBIND_NODE_TYPE_NAME,
                                                       NULL,
                                                       strdup("struct")),
                                      $2);
        }
        |
        TOK_UNION TOK_IDENTIFIER
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_NAME,
                                      genbind_new_node(GENBIND_NODE_TYPE_NAME,
                                                       NULL,
                                                       strdup("union")),
                                      $2);
        }
        |
        TOK_IDENTIFIER
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_NAME, NULL, $1);
        }
        ;

CType:
        CTypeSpecifier
        |
        CTypeSpecifier '*'
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_NAME, $1, strdup("*"));
        }

 /* type and identifier of a variable */
CTypeIdent:
        CType TOK_IDENTIFIER
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_IDENT, $1, $2);
        }
        |
        CType TOK_IDENTIFIER TOK_DBLCOLON TOK_IDENTIFIER
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_IDENT,
                        genbind_new_node(GENBIND_NODE_TYPE_IDENT, $1, $2), $4);
        }
        ;


CBlock:
        TOK_CCODE_LITERAL
        |
        CBlock TOK_CCODE_LITERAL
        {
          $$ = genbind_strapp($1, $2);
        }
        ;


ParameterList:
        CTypeIdent
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_PARAMETER, NULL, $1);
        }
        |
        ParameterList ',' CTypeIdent
        {
                $$ = genbind_node_prepend($1,
                                          genbind_new_node(
                                                  GENBIND_NODE_TYPE_PARAMETER,
                                                  NULL,
                                                  $3));
        }
        ;

Method:
        MethodType MethodDeclarator CBlock
        {
                $$ = add_method(genbind_ast, $1, $2, $3);
        }
        |
        MethodType MethodDeclarator ';'
        {
                $$ = add_method(genbind_ast, $1, $2, NULL);
        }
        ;

MethodType:
        TOK_INIT
        {
                $$ = GENBIND_METHOD_TYPE_INIT;
        }
        |
        TOK_FINI
        {
                $$ = GENBIND_METHOD_TYPE_FINI;
        }
        |
        TOK_METHOD
        {
                $$ = GENBIND_METHOD_TYPE_METHOD;
        }
        |
        TOK_GETTER
        {
                $$ = GENBIND_METHOD_TYPE_GETTER;
        }
        |
        TOK_SETTER
        {
                $$ = GENBIND_METHOD_TYPE_SETTER;
        }
        |
        TOK_PROTOTYPE
        {
                $$ = GENBIND_METHOD_TYPE_PROTOTYPE;
        }
        |
        BindingAndMethodType
        ;

BindingAndMethodType:
        TOK_PREFACE
        {
                $$ = GENBIND_METHOD_TYPE_PREFACE;
        }
        |
        TOK_PROLOGUE
        {
                $$ = GENBIND_METHOD_TYPE_PROLOGUE;
        }
        |
        TOK_EPILOGUE
        {
                $$ = GENBIND_METHOD_TYPE_EPILOGUE;
        }
        |
        TOK_POSTFACE
        {
                $$ = GENBIND_METHOD_TYPE_POSTFACE;
        }
        ;

MethodDeclarator:
        TOK_IDENTIFIER TOK_DBLCOLON TOK_IDENTIFIER '(' ParameterList ')'
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_CLASS,
                                      genbind_new_node(GENBIND_NODE_TYPE_IDENT,
                                                       $5,
                                                       $3),
                                      genbind_new_node(GENBIND_NODE_TYPE_IDENT,
                                                       NULL,
                                                       $1));
        }
        |
        TOK_IDENTIFIER TOK_DBLCOLON TOK_IDENTIFIER '(' ')'
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_CLASS,
                                      genbind_new_node(GENBIND_NODE_TYPE_IDENT,
                                                       NULL,
                                                       $3),
                                      genbind_new_node(GENBIND_NODE_TYPE_IDENT,
                                                       NULL,
                                                       $1));
        }
        |
        TOK_IDENTIFIER '(' ParameterList ')'
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_CLASS,
                                      $3,
                                      genbind_new_node(GENBIND_NODE_TYPE_IDENT,
                                                       NULL,
                                                       $1));
        }
        |
        TOK_IDENTIFIER '(' ')'
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_CLASS, NULL,
                                      genbind_new_node(GENBIND_NODE_TYPE_IDENT,
                                                       NULL,
                                                       $1));
        }
        ;


Class:
        TOK_CLASS TOK_IDENTIFIER '{' ClassArgs '}' ';'
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_CLASS, NULL,
                        genbind_new_node(GENBIND_NODE_TYPE_IDENT, $4, $2));
        }
        ;

ClassArgs:
        ClassArg
        |
        ClassArgs ClassArg
        {
                $$ = genbind_node_link($2, $1);
        }
        ;

ClassArg:
        TOK_PRIVATE CTypeIdent ';'
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_PRIVATE, NULL, $2);
        }
        |
        TOK_INTERNAL CTypeIdent ';'
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_INTERNAL, NULL, $2);
        }
        |
        TOK_PROPERTY Modifiers TOK_IDENTIFIER ';'
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_PROPERTY, NULL,
                        genbind_new_node(GENBIND_NODE_TYPE_MODIFIER,
                                genbind_new_node(GENBIND_NODE_TYPE_IDENT,
                                                 NULL,
                                                 $3),
                                         (void *)$2));
        }
        |
        TOK_FLAGS ClassFlags ';'
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_FLAGS, NULL, $2);
        }
        |
        BindingAndMethodType CBlock ';'
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_METHOD,
                        NULL,
                        genbind_new_node(GENBIND_NODE_TYPE_METHOD_TYPE,
                                genbind_new_node(GENBIND_NODE_TYPE_CDATA,
                                                 NULL,
                                                 $2),
                                (void *)$1));
        }
        ;

ClassFlags:
        TOK_IDENTIFIER
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_IDENT, NULL, $1);
        }
        |
        ClassFlags ',' TOK_IDENTIFIER
        {
                $$ = genbind_new_node(GENBIND_NODE_TYPE_IDENT, $1, $3);
        }
        ;

Modifiers:
        /* empty */
        {
            $$ = GENBIND_TYPE_NONE;
        }
        |
        Modifiers Modifier
        {
            $$ |= $2;
        }
        ;

Modifier:
        TOK_TYPE
        {
            $$ = GENBIND_TYPE_TYPE;
        }
        |
        TOK_UNSHARED
        {
            $$ = GENBIND_TYPE_UNSHARED;            
        }
        |
        TOK_SHARED
        {
            $$ = GENBIND_TYPE_NONE;            
        }
        ;

%%
