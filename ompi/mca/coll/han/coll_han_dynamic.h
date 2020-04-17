/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2020      Bull S.A.S. All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef MCA_COLL_HAN_DYNAMIC_H
#define MCA_COLL_HAN_DYNAMIC_H

#include "ompi_config.h"

#include "mpi.h"
#include "ompi/mca/mca.h"
#include "opal/util/output.h"
#include "ompi/mca/coll/base/coll_base_functions.h"


/* 
 * #################################################
 * # Dynamic rules global architecture description #
 * #################################################
 *
 * Han dynamic rules allow the user to define the collective
 * module to call depending the topological configuration of the
 * sub-communicators and the collective parameters. This mechanism
 * can also be used to fallback the main collective on another module.
 * The interface is described in coll_han_dynamic_file.h.
 *
 * #############################
 * # Collective module storage #
 * #############################
 * To be able to switch between multiple collective modules, han
 * directly accesses the module on the communicator. This information is
 * stored in the collective structure of the communicator during the collective
 * module choice at the communicator initialization. When han needs this
 * information for the first time, it identifies the modles by their name and
 * stores them in its module structure.
 * Then, the modules are identified by their identifier.
 *
 * #########################
 * # Dynamic rules storage #
 * #########################
 * There are two types of dynamic rules:
 *     - MCA parameter defined rules
 *     - File defined rules
 *
 * MCA parameter defined rules are stored in mca_coll_han_component.mca_rules.
 * This is a double indexed table. The first index is the coresponding collective
 * communication and the second index is the topological level aimed by the rule.
 * These parameters define the collective component to use for a specific
 * collective communication on a specific topologic level.
 *
 * File defined rules are stored in mca_coll_han_component.dynamic_rules.
 * These structures are defined bellow. The rule storage is directy deduced
 * from the rule file format.
 *
 * File defined rules precede MCA parameter defined rules.
 *
 * #######################
 * # Dynamic rules usage #
 * #######################
 * To choose which collective module to use on a specific configuration, han
 * adds an indirection on the collective call: dynamic choice functions. These
 * functions do not implement any collective. First, they try to find a dynamic
 * rule from file for the given collective. If there is not any rule for the
 * fiven configuration, MCA parameter defined rules are used. Once the module
 * to use is found, the correct collective implementation is called.
 *
 * This indirection is also used on the global communicator. This allows han
 * to provide a fallback mechanism considering the collective parameters.
 *
 * ##############################
 * # Dynamic rules choice logic #
 * ##############################
 * Dynamic rules choice is made with a stack logic. Each new rule precedes
 * already defined rules. MCA parameters rules are the stack base. When
 * a rule is needed, rules are read as a stack and the first corresponding
 * encountered is chosen.
 *
 * Consequences:
 *     - If a collective identifier appears multiple times, only the last
 *       will be considered
 *     - If a topological level appears multiple times for a collective,
 *       only the last will be considered
 *     - If configuration rules or message size rules are not stored
 *       by increasing value, some of them will not be considered
 */

BEGIN_C_DECLS

/* Dynamic rules support */
typedef enum COMPONENTS {
    SELF=0,
    BASIC,
    LIBNBC,
    TUNED,
    SM,
    SHARED,
    ADAPT,
    HAN,
    COMPONENTS_COUNT
} COMPONENT_T;

static const char *components_name[]={"self",
                                      "basic",
                                      "libnbc",
                                      "tuned",
                                      "sm",
                                      "shared",
                                      "adapt",
                                      "han"};

/* Topologic levels */
typedef enum TOPO_LVL {
    INTRA_NODE=0,
    INTER_NODE,
    /* Identifies the global communicator as a topologic level */
    GLOBAL_COMMUNICATOR,
    NB_TOPO_LVL
} TOPO_LVL_T;

/* Rule for a specific msg size
 * in a specific configuration
 * for a specific collective
 * in a specific topologic level */
typedef struct msg_size_rule_s {
    COLLTYPE_T collective_id;
    TOPO_LVL_T topologic_level;
    int configuration_size;

    /* Message size of the rule */
    int msg_size;

    /* Component to use on this specific configuration
     * and message size */
    COMPONENT_T component;
} msg_size_rule_t;

/* Rule for a specific configuration
 * considering a specific collective
 * in a specific topologic level */
typedef struct configuration_rule_s {
    COLLTYPE_T collective_id;
    TOPO_LVL_T topologic_level;

    /* Number of elements of the actual topologic level
     * per element of the upper topologic level */
    int configuration_size;

    /* Number of message size rules for this configuration */
    int nb_msg_size;

    /* Table of message size rules for this configuration */
    msg_size_rule_t *msg_size_rules;
} configuration_rule_t;

/* Set of dynamic rules for a specific collective
 * in a specific topologic level */
typedef struct topologic_rule_s {
    /* Collective identifier */
    COLLTYPE_T collective_id;

    /* Topologic level of the rule */
    TOPO_LVL_T topologic_level;

    /* Rule number */
    int nb_rules;

    /* Table of configuration rules
     * for this collective on this topologic level */
    configuration_rule_t *configuration_rules;
} topologic_rule_t;

/* Set of dynamic rules for a collective */
typedef struct collective_rule_s {
    COLLTYPE_T collective_id;

    /* Number of topologic level for this collective */
    int nb_topologic_levels;

    /* Table of topologic level rules
     * for this collective */
    topologic_rule_t *topologic_rules;
} collective_rule_t;

/* Global dynamic rules structure */
typedef struct mca_coll_han_dynamic_rule_s {
    int nb_collectives;
    collective_rule_t *collective_rules;
} mca_coll_han_dynamic_rules_t;

/* Module storage */
typedef struct collective_module_storage_s {
    /* Module */
    mca_coll_base_module_t *module_handler;
} collective_module_storage_t;

/* Table of module storage */
typedef struct mca_coll_han_collective_modules_storage_s {
    /*  */
    collective_module_storage_t modules[COMPONENTS_COUNT];
} mca_coll_han_collective_modules_storage_t;

/* Tests if a dynamic collective is implemented */
bool mca_coll_han_is_coll_dynamic_implemented(COLLTYPE_T coll_id);

END_C_DECLS
#endif
