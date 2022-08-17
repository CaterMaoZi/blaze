#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "shared.h"

static void
register_block_property(int id, char * name, int value_count, char * * values) {
    block_property_spec prop_spec = {0};
    prop_spec.value_count = value_count;

    int name_size = strlen(name);

    unsigned char * tape = prop_spec.tape;
    *tape = name_size;
    tape++;
    memcpy(tape, name, name_size);
    tape += name_size;

    for (int i = 0; i < value_count; i++) {
        int value_size = strlen(values[i]);
        *tape = value_size;
        tape++;
        memcpy(tape, values[i], value_size);
        tape += value_size;
    }

    serv->block_property_specs[id] = prop_spec;
}

static void
register_property_v(int id, char * name, int value_count, ...) {
    va_list ap;
    va_start(ap, value_count);

    char * values[value_count];
    for (int i = 0; i < value_count; i++) {
        values[i] = va_arg(ap, char *);
    }

    va_end(ap);

    register_block_property(id, name, value_count, values);
}

static void
register_range_property(int id, char * name, int min, int max) {
    int value_count = max - min + 1;
    char * values[value_count];
    char buf[256];
    char * cursor = buf;
    char * end = buf + sizeof buf;

    for (int i = min; i <= max; i++) {
        int size = snprintf(cursor, end - cursor, "%d", i);
        values[i - min] = cursor;
        cursor += size;
        cursor++; // terminating null character
    }

    register_block_property(id, name, value_count, values);
}

static void
register_bool_property(int id, char * name) {
    register_property_v(id, name, 2, "true", "false");
}

static void
add_block_property(block_properties * props, int id, char * default_value) {
    // figure out index of default value
    block_property_spec * spec = serv->block_property_specs + id;
    int default_value_index = spec->value_count;
    int tape_index = 1 + spec->tape[0];

    String default_string = STR(default_value);

    for (int i = 0; i < spec->value_count; i++) {
        String value = {
            .size = spec->tape[tape_index],
            .data = spec->tape + tape_index + 1
        };
        if (net_string_equal(default_string, value)) {
            default_value_index = i;
            break;
        }

        tape_index += value.size + 1;
    }
    assert(default_value_index < spec->value_count);

    int prop_index = props->property_count;
    props->property_specs[prop_index] = id;
    props->default_value_indices[prop_index] = default_value_index;

    props->property_count++;
}

static int
count_block_states(block_properties * props) {
    int block_states = 1;
    for (int i = 0; i < props->property_count; i++) {
        block_states *= serv->block_property_specs[props->property_specs[i]].value_count;
    }
    return block_states;
}

static void
finalise_block_props(block_properties * props) {
    props->base_state = serv->actual_block_state_count;

    i32 block_type = props - serv->block_properties_table;
    int block_states = count_block_states(props);

    assert(serv->actual_block_state_count + block_states <= ARRAY_SIZE(serv->block_type_by_state));

    for (int i = 0; i < block_states; i++) {
        serv->block_type_by_state[serv->actual_block_state_count + i] = block_type;
    }

    serv->actual_block_state_count += block_states;
}

static i32
register_block_type(char * resource_loc) {
    String key = STR(resource_loc);
    resource_loc_table * table = &serv->block_resource_table;
    i32 block_type = serv->block_type_count;
    serv->block_type_count++;
    register_resource_loc(key, block_type, table);
    assert(net_string_equal(key, get_resource_loc(block_type, table)));
    assert(block_type == resolve_resource_loc_id(key, table));
    return block_type;
}

static void SetParticularModelForAllStates(block_properties * props, u8 * modelByState, i32 modelId) {
    int block_states = count_block_states(props);
    for (int i = 0; i < block_states; i++) {
        int j = props->base_state + i;
        modelByState[j] = modelId;
    }
}

static void SetCollisionModelForAllStates(block_properties * props, i32 modelId) {
    SetParticularModelForAllStates(props, serv->collisionModelByState, modelId);
}

static void SetSupportModelForAllStates(block_properties * props, i32 modelId) {
    SetParticularModelForAllStates(props, serv->supportModelByState, modelId);
}

// NOTE(traks): for this, some relevant vanilla code is:
// - useShapeForLightOcclusion: false = can just use empty model here
// - getOcclusionShape: shape for occlusion if above = true
static void SetLightBlockingModelForAllStates(block_properties * props, i32 modelId) {
    SetParticularModelForAllStates(props, serv->lightBlockingModelByState, modelId);
}

static void SetAllModelsForAllStatesIndividually(block_properties * props, i32 collisionModelId, i32 supportModelId, i32 lightModelId) {
    SetCollisionModelForAllStates(props, collisionModelId);
    SetSupportModelForAllStates(props, supportModelId);
    SetLightBlockingModelForAllStates(props, lightModelId);
}

static void SetAllModelsForAllStates(block_properties * props, i32 modelId) {
    SetAllModelsForAllStatesIndividually(props, modelId, modelId, modelId);
}

// NOTE(traks): for full blocks it doesn't really matter what the light
// reduction is, because the light will get blocked anyway.
// Relevant vanilla code is:
// - getLightBlock: how much light is blocked
// - propagatesSkylightDown: if max sky light can pass through unchanged
static void SetLightReductionForAllStates(block_properties * props, i32 lightReduction) {
    i32 stateCount = count_block_states(props);
    for (i32 i = 0; i < stateCount; i++) {
        i32 j = props->base_state + i;
        serv->lightReductionByState[j] = lightReduction & 0xff;
    }
}

static void SetLightReductionAuto(block_properties * props) {
    i32 stateCount = count_block_states(props);
    for (i32 i = 0; i < stateCount; i++) {
        i32 blockState = props->base_state + i;
        block_state_info info = describe_block_state(blockState);
        i32 lightReduction = (info.waterlogged ? 1 : 0);
        serv->lightReductionByState[blockState] = lightReduction;
    }
}

static void
InitSimpleBlockWithModels(char * resource_loc, i32 collisionModelId, i32 supportModelId, i32 lightModelId, i32 lightReduction) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    finalise_block_props(props);
    SetCollisionModelForAllStates(props, collisionModelId);
    SetSupportModelForAllStates(props, supportModelId);
    SetLightBlockingModelForAllStates(props, lightModelId);
    SetLightReductionForAllStates(props, lightReduction);
}

static void
init_simple_block(char * resource_loc, int modelId, i32 lightReduction) {
    InitSimpleBlockWithModels(resource_loc, modelId, modelId, modelId, lightReduction);
}

static void InitSimpleEmptyBlock(char * resourceLoc) {
    InitSimpleBlockWithModels(resourceLoc, BLOCK_MODEL_EMPTY, BLOCK_MODEL_EMPTY, BLOCK_MODEL_EMPTY, 0);
}

static void InitSimpleFullBlock(char * resourceLoc) {
    InitSimpleBlockWithModels(resourceLoc, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 15);
}

static void
init_sapling(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_STAGE, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_propagule(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_STAGE, "0");
    add_block_property(props, BLOCK_PROPERTY_AGE_4, "0");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_HANGING, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_pillar(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AXIS, "y");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(props, 15);
}

static void
init_leaves(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    props->type_tags |= (u32) 1 << BLOCK_TAG_LEAVES;
    add_block_property(props, BLOCK_PROPERTY_DISTANCE, "7");
    add_block_property(props, BLOCK_PROPERTY_PERSISTENT, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    SetCollisionModelForAllStates(props, BLOCK_MODEL_FULL);
    SetSupportModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 1);
}

static void
init_bed(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_OCCUPIED, "false");
    add_block_property(props, BLOCK_PROPERTY_BED_PART, "foot");
    finalise_block_props(props);

    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int facing = info.horizontal_facing;
        if (info.bed_part == BED_PART_HEAD) {
            facing = get_opposite_direction(facing);
        }
        int model_index;
        switch (info.horizontal_facing) {
        case DIRECTION_POS_X: model_index = BLOCK_MODEL_BED_FOOT_POS_X; break;
        case DIRECTION_POS_Z: model_index = BLOCK_MODEL_BED_FOOT_POS_Z; break;
        case DIRECTION_NEG_X: model_index = BLOCK_MODEL_BED_FOOT_NEG_X; break;
        case DIRECTION_NEG_Z: model_index = BLOCK_MODEL_BED_FOOT_NEG_Z; break;
        }
        serv->collisionModelByState[block_state] = model_index;
        serv->supportModelByState[block_state] = model_index;
    }

    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_slab(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_SLAB_TYPE, "bottom");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int model_index;
        switch (info.slab_type) {
        case SLAB_TOP: model_index = BLOCK_MODEL_TOP_SLAB; break;
        case SLAB_BOTTOM: model_index = BLOCK_MODEL_Y_8; break;
        case SLAB_DOUBLE: model_index = BLOCK_MODEL_FULL; break;
        }
        serv->collisionModelByState[block_state] = model_index;
        serv->supportModelByState[block_state] = model_index;
        serv->lightBlockingModelByState[block_state] = model_index;
    }
    SetLightReductionAuto(props);
}

static void
init_sign(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ROTATION_16, "0");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_wall_sign(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_stair_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    props->type_tags |= (u32) 1 << BLOCK_TAG_STAIRS;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_HALF, "bottom");
    add_block_property(props, BLOCK_PROPERTY_STAIRS_SHAPE, "straight");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    // TODO(traks): block models
    SetLightReductionAuto(props);
}

static void
init_tall_plant(char * resource_loc, i32 lightReduction) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "lower");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, lightReduction);
}

static void
init_glazed_terracotta(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(props, 15);
}

static void
init_shulker_box_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    props->type_tags |= (u32) 1 << BLOCK_TAG_SHULKER_BOX;
    add_block_property(props, BLOCK_PROPERTY_FACING, "up");
    finalise_block_props(props);
    // TODO(traks): block models
    SetLightReductionForAllStates(props, 1);
}

static void
init_wall_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    props->type_tags |= ((u32) 1) << BLOCK_TAG_WALL;
    add_block_property(props, BLOCK_PROPERTY_WALL_POS_X, "none");
    add_block_property(props, BLOCK_PROPERTY_WALL_NEG_Z, "none");
    add_block_property(props, BLOCK_PROPERTY_WALL_POS_Z, "none");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "true");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_WALL_NEG_X, "none");
    finalise_block_props(props);
    // TODO(traks): block models
    SetLightReductionAuto(props);
}

static void
init_pressure_plate(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_pane(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    props->type_tags |= (u32) 1 << BLOCK_TAG_PANE_LIKE;
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    finalise_block_props(props);
    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int flags = (info.pos_x << 3) | (info.neg_x << 2) | (info.pos_z << 1) | info.neg_z;
        int model_index = BLOCK_MODEL_PANE_CENTRE + flags;
        serv->collisionModelByState[block_state] = model_index;
        serv->supportModelByState[block_state] = model_index;
    }
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_fence(char * resource_loc, int wooden) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    if (wooden) {
        props->type_tags |= (u32) 1 << BLOCK_TAG_WOODEN_FENCE;
    }
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    finalise_block_props(props);
    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int flags = (info.pos_x << 3) | (info.neg_x << 2) | (info.pos_z << 1) | info.neg_z;
        int model_index = BLOCK_MODEL_FENCE_CENTRE + flags;
        serv->collisionModelByState[block_state] = model_index;
        serv->supportModelByState[block_state] = model_index;
    }
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_door_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "lower");
    add_block_property(props, BLOCK_PROPERTY_DOOR_HINGE, "left");
    add_block_property(props, BLOCK_PROPERTY_OPEN, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    // TODO(traks): block models
    SetLightReductionAuto(props);
}

static void
init_button(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ATTACH_FACE, "wall");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_trapdoor_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_HALF, "bottom");
    add_block_property(props, BLOCK_PROPERTY_OPEN, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_fence_gate(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    props->type_tags |= (u32) 1 << BLOCK_TAG_FENCE_GATE;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_IN_WALL, "false");
    add_block_property(props, BLOCK_PROPERTY_OPEN, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int model_index;
        if (info.open) {
            model_index = BLOCK_MODEL_EMPTY;
        } else if (get_direction_axis(info.horizontal_facing) == AXIS_X) {
            model_index = BLOCK_MODEL_FENCE_GATE_FACING_X;
        } else {
            model_index = BLOCK_MODEL_FENCE_GATE_FACING_Z;
        }
        serv->collisionModelByState[block_state] = model_index;
        serv->supportModelByState[block_state] = model_index;
    }
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_mushroom_block(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_NEG_Y, "true");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "true");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "true");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "true");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "true");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "true");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(props, 15);
}

static void
init_skull_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ROTATION_16, "0");
    finalise_block_props(props);
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_wall_skull_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_anvil_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_banner(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ROTATION_16, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_wall_banner(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_coral(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_coral_fan(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_coral_wall_fan(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_snowy_grassy_block(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_SNOWY, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(props, 15);
}

static void
init_redstone_ore(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(props, 15);
}

static void
init_cauldron(char * resource_loc, int layered) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    if (layered) {
        add_block_property(props, BLOCK_PROPERTY_LEVEL_CAULDRON, "1");
    }
    finalise_block_props(props);
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_candle(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_CANDLES, "1");
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_candle_cake(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    finalise_block_props(props);
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void
init_amethyst_cluster(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "up");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void InitMultiFaceBlock(char * resourceLoc) {
    i32 blockType = register_block_type(resourceLoc);
    block_properties * props = serv->block_properties_table + blockType;
    add_block_property(props, BLOCK_PROPERTY_NEG_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);
}

static void InitFlowerPot(char * resourceLoc) {
    InitSimpleBlockWithModels(resourceLoc, BLOCK_MODEL_FLOWER_POT, BLOCK_MODEL_FLOWER_POT, BLOCK_MODEL_EMPTY, 0);
}

static void InitCarpet(char * resourceLoc) {
    InitSimpleBlockWithModels(resourceLoc, BLOCK_MODEL_Y_1, BLOCK_MODEL_Y_1, BLOCK_MODEL_EMPTY, 0);
}

typedef struct {
    float min_a;
    float min_b;
    float max_a;
    float max_b;
} block_box_face;

typedef struct {
    float min_a;
    float min_b;
    float max_a;
    float max_b;
    float axis_min;
    float axis_max;
    float axis_cut;
} intersect_test;

static int
block_boxes_contain_face(int box_count, BoundingBox * boxes,
        BoundingBox slice, int direction) {
    // intersect boxes with 1x1x1 cube and get the faces
    int face_count = 0;
    block_box_face faces[box_count];
    // @NOTE(traks) All block models are currently aligned to the pixel grid, so
    // the epsilon can be fairly large. Bamboo is aligned to a quarter of a
    // pixel grid, but we currently don't use this algorithm for bamboo.
    float eps = 0.001;

    for (int i = 0; i < box_count; i++) {
        BoundingBox box = boxes[i];
        intersect_test test;

        switch (direction) {
        case DIRECTION_NEG_Y: test = (intersect_test) {box.minX, box.minZ, box.maxX, box.maxZ, box.minY, box.maxY, slice.minY}; break;
        case DIRECTION_POS_Y: test = (intersect_test) {box.minX, box.minZ, box.maxX, box.maxZ, box.minY, box.maxY, slice.maxY}; break;
        case DIRECTION_NEG_Z: test = (intersect_test) {box.minX, box.minY, box.maxX, box.maxY, box.minZ, box.maxZ, slice.minZ}; break;
        case DIRECTION_POS_Z: test = (intersect_test) {box.minX, box.minY, box.maxX, box.maxY, box.minZ, box.maxZ, slice.maxZ}; break;
        case DIRECTION_NEG_X: test = (intersect_test) {box.minY, box.minZ, box.maxY, box.maxZ, box.minX, box.maxX, slice.minX}; break;
        case DIRECTION_POS_X: test = (intersect_test) {box.minY, box.minZ, box.maxY, box.maxZ, box.minX, box.maxX, slice.maxX}; break;
        }

        if (test.axis_min <= test.axis_cut + eps && test.axis_cut <= test.axis_max + eps) {
            faces[face_count] = (block_box_face) {test.min_a, test.min_b, test.max_a, test.max_b};
            face_count++;
        }
    }

    block_box_face test;
    switch (direction) {
    case DIRECTION_NEG_Y: test = (block_box_face) {slice.minX, slice.minZ, slice.maxX, slice.maxZ}; break;
    case DIRECTION_POS_Y: test = (block_box_face) {slice.minX, slice.minZ, slice.maxX, slice.maxZ}; break;
    case DIRECTION_NEG_Z: test = (block_box_face) {slice.minX, slice.minY, slice.maxX, slice.maxY}; break;
    case DIRECTION_POS_Z: test = (block_box_face) {slice.minX, slice.minY, slice.maxX, slice.maxY}; break;
    case DIRECTION_NEG_X: test = (block_box_face) {slice.minY, slice.minZ, slice.maxY, slice.maxZ}; break;
    case DIRECTION_POS_X: test = (block_box_face) {slice.minY, slice.minZ, slice.maxY, slice.maxZ}; break;
    }

    // start at minimum a and b. First we try to move our best_b to the
    // maximum b by checking whether faces contain the coordinate
    // (best_a, best_b). After that we can move best_a forward by a bit. We then
    // reset best_b and repeat the whole process, until best_a doesn't move
    // anymore or hits the maximum a.
    //
    // Currently our algorithm assumes that each of the faces has positive area!
    // So no lines as input please. This is to ensure progress is made in the
    // loops below.

    float best_a = test.min_a;
    float best_b = test.min_b;

    for (int loop = 0; loop < 1000; loop++) {
        float old_best_a = best_a;

        // first try to move best_b forward
        float big_number = 1000;
        float min_found_a = big_number;

        for (;;) {
            float old_best_b = best_b;

            for (int i = 0; i < face_count; i++) {
                block_box_face * face = faces + i;

                if (face->min_a <= best_a + eps && best_a <= face->max_a + eps
                        && face->min_b <= best_b + eps && best_b <= face->max_b + eps) {
                    // face contains our coordinate, so move best_b forward
                    best_b = face->max_b;
                    min_found_a = MIN(face->max_a, min_found_a);
                }
            }

            if (old_best_b == best_b) {
                // best b didn't change, so test face not contained inside our
                // list of faces
                return 0;
            }

            if (best_b + eps >= test.max_b) {
                // reached maximum b, so move best_a forward and reset best_b
                best_b = test.min_b;
                best_a = min_found_a;
                break;
            }
        }

        if (best_a + eps >= test.max_a) {
            // reached maximum a, so done!
            return 1;
        }

        if (old_best_a == best_a) {
            // best_a didn't change, so test face not contained inside our
            // list of faces
            return 0;
        }
    }

    // maximum number of loops reached, very bad!
    assert(0);
    return 0;
}

static int
block_boxes_intersect_face(int box_count, BoundingBox * boxes,
        BoundingBox slice, int direction) {
    switch (direction) {
    case DIRECTION_NEG_Y: slice.maxY = slice.minY; break;
    case DIRECTION_POS_Y: slice.minY = slice.maxY; break;
    case DIRECTION_NEG_Z: slice.maxZ = slice.minZ; break;
    case DIRECTION_POS_Z: slice.minZ = slice.maxZ; break;
    case DIRECTION_NEG_X: slice.maxX = slice.minX; break;
    case DIRECTION_POS_X: slice.minX = slice.maxX; break;
    }

    // check if the selected face intersects any of the boxes
    for (int i = 0; i < box_count; i++) {
        BoundingBox * box = boxes + i;
        if (box->minX <= slice.maxX && box->maxX >= slice.minX
                && box->minY <= slice.maxY && box->maxY >= slice.minY
                && box->minZ <= slice.maxZ && box->maxZ >= slice.minZ) {
            return 1;
        }
    }
    return 0;
}

static void
register_block_model(int index, int box_count, BoundingBox * pixel_boxes) {
    BlockModel * model = serv->staticBlockModels + index;
    assert(box_count < ARRAY_SIZE(model->boxes));
    model->size = box_count;
    for (int i = 0; i < box_count; i++) {
        model->boxes[i] = pixel_boxes[i];
        model->boxes[i].minX /= 16;
        model->boxes[i].minY /= 16;
        model->boxes[i].minZ /= 16;
        model->boxes[i].maxX /= 16;
        model->boxes[i].maxY /= 16;
        model->boxes[i].maxZ /= 16;
    }

    for (int dir = 0; dir < 6; dir++) {
        BoundingBox full_box = {0, 0, 0, 1, 1, 1};
        if (block_boxes_contain_face(box_count, model->boxes, full_box, dir)) {
            model->fullFaces |= 1 << dir;
        }

        BoundingBox pole = {7.0f / 16, 0, 7.0f / 16, 9.0f / 16, 1, 9.0f / 16};
        if (block_boxes_contain_face(box_count, model->boxes, pole, dir)) {
            model->poleFaces |= 1 << dir;
        }

        if (block_boxes_intersect_face(box_count, model->boxes, full_box, dir)) {
            model->nonEmptyFaces |= 1 << dir;
        }
    }
}

static void RegisterBlockModel(i32 id, BlockModel model) {
    serv->staticBlockModels[id] = model;
}

// @NOTE(traks) all thes rotation functions assume the coordinates of the box
// are in pixel coordinates

static BoundingBox
rotate_block_box_clockwise(BoundingBox box) {
    BoundingBox res = {16 - box.maxZ , box.minY, box.minX, 16 - box.minZ, box.maxY, box.maxX};
    return res;
}

static BoundingBox
rotate_block_box_180(BoundingBox box) {
    return rotate_block_box_clockwise(rotate_block_box_clockwise(box));
}

static BoundingBox
rotate_block_box_counter_clockwise(BoundingBox box) {
    return rotate_block_box_180(rotate_block_box_clockwise(box));
}

static void
register_cross_block_models(int start_index, BoundingBox centre_box,
        BoundingBox neg_z_box, BoundingBox z_box) {
    for (int i = 0; i < 16; i++) {
        int neg_z = (i & 0x1);
        int pos_z = (i & 0x2);
        int neg_x = (i & 0x4);
        int pos_x = (i & 0x8);

        int box_count = 0;
        BoundingBox boxes[2];

        if (neg_z && pos_z) {
            boxes[box_count] = z_box;
            box_count++;
        } else if (neg_z) {
            boxes[box_count] = neg_z_box;
            box_count++;
        } else if (pos_z) {
            boxes[box_count] = rotate_block_box_180(neg_z_box);
            box_count++;
        }

        if (neg_x && pos_x) {
            boxes[box_count] = rotate_block_box_clockwise(z_box);
            box_count++;
        } else if (neg_x) {
            boxes[box_count] = rotate_block_box_counter_clockwise(neg_z_box);
            box_count++;
        } else if (pos_x) {
            boxes[box_count] = rotate_block_box_clockwise(neg_z_box);
            box_count++;
        }

        if (box_count == 0) {
            // not connected to any edges
            boxes[box_count] = centre_box;
            box_count++;
        }

        register_block_model(start_index + i, box_count, boxes);
    }
}

static void DivideOutCollisionModelPixels(BlockModel * model) {
    for (i32 i = 0; i < model->size; i++) {
        model->boxes[i].minX /= 16;
        model->boxes[i].minY /= 16;
        model->boxes[i].minZ /= 16;
        model->boxes[i].maxX /= 16;
        model->boxes[i].maxY /= 16;
        model->boxes[i].maxZ /= 16;
    }
}

void
init_block_data(void) {
    register_block_model(BLOCK_MODEL_EMPTY, 0, NULL);
    // @NOTE(traks) this initialises the full block model
    for (int y = 1; y <= 16; y++) {
        BoundingBox box = {0, 0, 0, 16, y, 16};
        register_block_model(BLOCK_MODEL_EMPTY + y, 1, &box);
    }
    BoundingBox flower_pot_box = {5, 0, 5, 11, 6, 11};
    register_block_model(BLOCK_MODEL_FLOWER_POT, 1, &flower_pot_box);
    BoundingBox cactus_box = {1, 0, 1, 15, 15, 15};
    register_block_model(BLOCK_MODEL_CACTUS, 1, &cactus_box);
    BoundingBox composter_boxes[] = {
        {0, 0, 0, 16, 2, 16}, // bottom
        {0, 0, 0, 2, 16, 16}, // wall neg x
        {0, 0, 0, 16, 16, 2}, // wall neg z
        {14, 0, 0, 16, 16, 16}, // wall pos x
        {0, 0, 14, 16, 16, 16}, // wall pos z
    };
    register_block_model(BLOCK_MODEL_COMPOSTER, ARRAY_SIZE(composter_boxes), composter_boxes);
    BoundingBox honey_box = {1, 0, 1, 15, 15, 15};
    register_block_model(BLOCK_MODEL_HONEY_BLOCK, 1, &honey_box);
    BoundingBox fence_gate_facing_x_box = {6, 0, 0, 10, 24, 16};
    BoundingBox fence_gate_facing_z_box = {0, 0, 6, 16, 24, 10};
    register_block_model(BLOCK_MODEL_FENCE_GATE_FACING_X, 1, &fence_gate_facing_x_box);
    register_block_model(BLOCK_MODEL_FENCE_GATE_FACING_Z, 1, &fence_gate_facing_z_box);

    BlockModel centredBambooModel = {
        .size = 1,
        .nonEmptyFaces = (1 << DIRECTION_NEG_Y) | (1 << DIRECTION_POS_Y),
        .boxes = {{6.5f, 0, 6.5f, 9.5f, 16, 9.5f}}
    };
    DivideOutCollisionModelPixels(&centredBambooModel);
    RegisterBlockModel(BLOCK_MODEL_CENTRED_BAMBOO, centredBambooModel);

    BoundingBox pane_centre_box = {7, 0, 7, 9, 16, 9};
    BoundingBox pane_neg_z_box = {7, 0, 0, 9, 16, 9};
    BoundingBox pane_z_box = {7, 0, 0, 9, 16, 16};
    register_cross_block_models(BLOCK_MODEL_PANE_CENTRE, pane_centre_box,
            pane_neg_z_box, pane_z_box);

    BoundingBox fence_centre_box = {6, 0, 6, 10, 24, 10};
    BoundingBox fence_neg_z_box = {6, 0, 0, 10, 24, 10};
    BoundingBox fence_z_box = {6, 0, 0, 10, 24, 16};
    register_cross_block_models(BLOCK_MODEL_FENCE_CENTRE, fence_centre_box,
            fence_neg_z_box, fence_z_box);

    BoundingBox boxes_foot_pos_x[] = {
        {0, 3, 0, 16, 9, 16}, // horizontal part
        {0, 0, 0, 3, 3, 3}, // leg 1
        {0, 0, 13, 3, 3, 16}, // leg 2
    };
    for (int i = 0; i < 4; i++) {
        int model_index = BLOCK_MODEL_BED_FOOT_POS_X + i;
        register_block_model(model_index, ARRAY_SIZE(boxes_foot_pos_x), boxes_foot_pos_x);
        for (int j = 0; j < ARRAY_SIZE(boxes_foot_pos_x); j++) {
            boxes_foot_pos_x[i] = rotate_block_box_clockwise(boxes_foot_pos_x[i]);
        }
    }

    BoundingBox lectern_boxes[] = {
        {0, 0, 0, 16, 2, 16}, // base
        {4, 2, 4, 12, 14, 12}, // post
    };
    register_block_model(BLOCK_MODEL_LECTERN, ARRAY_SIZE(lectern_boxes), lectern_boxes);
    BoundingBox slab_top_box = {0, 8, 0, 16, 16, 16};
    register_block_model(BLOCK_MODEL_TOP_SLAB, 1, &slab_top_box);
    BoundingBox lily_pad_box = {1, 0, 1, 15, 1.5f, 15};
    register_block_model(BLOCK_MODEL_LILY_PAD, 1, &lily_pad_box);
    BoundingBox scaffoldingBoxes[] = {
        0, 14, 0, 16, 16, 16, // top part
        0, 0, 0, 2, 16, 2, // leg 1
        14, 0, 0, 16, 16, 2, // leg 2
        0, 0, 14, 2, 16, 16, // leg 3
        14, 0, 14, 16, 16, 16, // leg 4
    };
    register_block_model(BLOCK_MODEL_SCAFFOLDING, 5, scaffoldingBoxes);

    register_bool_property(BLOCK_PROPERTY_ATTACHED, "attached");
    register_bool_property(BLOCK_PROPERTY_BOTTOM, "bottom");
    register_bool_property(BLOCK_PROPERTY_CONDITIONAL, "conditional");
    register_bool_property(BLOCK_PROPERTY_DISARMED, "disarmed");
    register_bool_property(BLOCK_PROPERTY_DRAG, "drag");
    register_bool_property(BLOCK_PROPERTY_ENABLED, "enabled");
    register_bool_property(BLOCK_PROPERTY_EXTENDED, "extended");
    register_bool_property(BLOCK_PROPERTY_EYE, "eye");
    register_bool_property(BLOCK_PROPERTY_FALLING, "falling");
    register_bool_property(BLOCK_PROPERTY_HANGING, "hanging");
    register_bool_property(BLOCK_PROPERTY_HAS_BOTTLE_0, "has_bottle_0");
    register_bool_property(BLOCK_PROPERTY_HAS_BOTTLE_1, "has_bottle_1");
    register_bool_property(BLOCK_PROPERTY_HAS_BOTTLE_2, "has_bottle_2");
    register_bool_property(BLOCK_PROPERTY_HAS_RECORD, "has_record");
    register_bool_property(BLOCK_PROPERTY_HAS_BOOK, "has_book");
    register_bool_property(BLOCK_PROPERTY_INVERTED, "inverted");
    register_bool_property(BLOCK_PROPERTY_IN_WALL, "in_wall");
    register_bool_property(BLOCK_PROPERTY_LIT, "lit");
    register_bool_property(BLOCK_PROPERTY_LOCKED, "locked");
    register_bool_property(BLOCK_PROPERTY_OCCUPIED, "occupied");
    register_bool_property(BLOCK_PROPERTY_OPEN, "open");
    register_bool_property(BLOCK_PROPERTY_PERSISTENT, "persistent");
    register_bool_property(BLOCK_PROPERTY_POWERED, "powered");
    register_bool_property(BLOCK_PROPERTY_SHORT_PISTON, "short");
    register_bool_property(BLOCK_PROPERTY_SIGNAL_FIRE, "signal_fire");
    register_bool_property(BLOCK_PROPERTY_SNOWY, "snowy");
    register_bool_property(BLOCK_PROPERTY_TRIGGERED, "triggered");
    register_bool_property(BLOCK_PROPERTY_UNSTABLE, "unstable");
    register_bool_property(BLOCK_PROPERTY_WATERLOGGED, "waterlogged");
    register_bool_property(BLOCK_PROPERTY_VINE_END, "vine_end");
    register_bool_property(BLOCK_PROPERTY_BERRIES, "berries");
    register_bool_property(BLOCK_PROPERTY_BLOOM, "bloom");
    register_bool_property(BLOCK_PROPERTY_SHRIEKING, "shrieking");
    register_bool_property(BLOCK_PROPERTY_CAN_SUMMON, "can_summon");
    register_property_v(BLOCK_PROPERTY_HORIZONTAL_AXIS, "axis", 2, "x", "z");
    register_property_v(BLOCK_PROPERTY_AXIS, "axis", 3, "x", "y", "z");
    register_bool_property(BLOCK_PROPERTY_POS_Y, "up");
    register_bool_property(BLOCK_PROPERTY_NEG_Y, "down");
    register_bool_property(BLOCK_PROPERTY_NEG_Z, "north");
    register_bool_property(BLOCK_PROPERTY_POS_X, "east");
    register_bool_property(BLOCK_PROPERTY_POS_Z, "south");
    register_bool_property(BLOCK_PROPERTY_NEG_X, "west");
    register_property_v(BLOCK_PROPERTY_FACING, "facing", 6, "north", "east", "south", "west", "up", "down");
    register_property_v(BLOCK_PROPERTY_FACING_HOPPER, "facing", 5, "down", "north", "south", "west", "east");
    register_property_v(BLOCK_PROPERTY_HORIZONTAL_FACING, "facing", 4, "north", "south", "west", "east");
    register_property_v(BLOCK_PROPERTY_JIGSAW_ORIENTATION, "orientation", 12, "down_east", "down_north", "down_south", "down_west", "up_east", "up_north", "up_south", "up_west", "west_up", "east_up", "north_up", "south_up");
    register_property_v(BLOCK_PROPERTY_ATTACH_FACE, "face", 3, "floor", "wall", "ceiling");
    register_property_v(BLOCK_PROPERTY_BELL_ATTACHMENT, "attachment", 4, "floor", "ceiling", "single_wall", "double_wall");
    register_property_v(BLOCK_PROPERTY_WALL_POS_X, "east", 3, "none", "low", "tall");
    register_property_v(BLOCK_PROPERTY_WALL_NEG_Z, "north", 3, "none", "low", "tall");
    register_property_v(BLOCK_PROPERTY_WALL_POS_Z, "south", 3, "none", "low", "tall");
    register_property_v(BLOCK_PROPERTY_WALL_NEG_X, "west", 3, "none", "low", "tall");
    register_property_v(BLOCK_PROPERTY_REDSTONE_POS_X, "east", 3, "up", "side", "none");
    register_property_v(BLOCK_PROPERTY_REDSTONE_NEG_Z, "north", 3, "up", "side", "none");
    register_property_v(BLOCK_PROPERTY_REDSTONE_POS_Z, "south", 3, "up", "side", "none");
    register_property_v(BLOCK_PROPERTY_REDSTONE_NEG_X, "west", 3, "up", "side", "none");
    register_property_v(BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "half", 2, "upper", "lower");
    register_property_v(BLOCK_PROPERTY_HALF, "half", 2, "top", "bottom");
    register_property_v(BLOCK_PROPERTY_RAIL_SHAPE, "shape", 10, "north_south", "east_west", "ascending_east", "ascending_west", "ascending_north", "ascending_south", "south_east", "south_west", "north_west", "north_east");
    register_property_v(BLOCK_PROPERTY_RAIL_SHAPE_STRAIGHT, "shape", 6, "north_south", "east_west", "ascending_east", "ascending_west", "ascending_north", "ascending_south");
    register_range_property(BLOCK_PROPERTY_AGE_1, "age", 0, 1);
    register_range_property(BLOCK_PROPERTY_AGE_2, "age", 0, 2);
    register_range_property(BLOCK_PROPERTY_AGE_3, "age", 0, 3);
    register_range_property(BLOCK_PROPERTY_AGE_4, "age", 0, 4);
    register_range_property(BLOCK_PROPERTY_AGE_5, "age", 0, 5);
    register_range_property(BLOCK_PROPERTY_AGE_7, "age", 0, 7);
    register_range_property(BLOCK_PROPERTY_AGE_15, "age", 0, 15);
    register_range_property(BLOCK_PROPERTY_AGE_25, "age", 0, 25);
    register_range_property(BLOCK_PROPERTY_BITES, "bites", 0, 6);
    register_range_property(BLOCK_PROPERTY_CANDLES, "candles", 1, 4);
    register_range_property(BLOCK_PROPERTY_DELAY, "delay", 1, 4);
    register_range_property(BLOCK_PROPERTY_DISTANCE, "distance", 1, 7);
    register_range_property(BLOCK_PROPERTY_EGGS, "eggs", 1, 4);
    register_range_property(BLOCK_PROPERTY_HATCH, "hatch", 0, 2);
    register_range_property(BLOCK_PROPERTY_LAYERS, "layers", 1, 8);
    register_range_property(BLOCK_PROPERTY_LEVEL_CAULDRON, "level", 1, 3);
    register_range_property(BLOCK_PROPERTY_LEVEL_COMPOSTER, "level", 0, 8);
    register_range_property(BLOCK_PROPERTY_LEVEL_HONEY, "honey_level", 0, 5);
    register_range_property(BLOCK_PROPERTY_LEVEL, "level", 0, 15);
    register_range_property(BLOCK_PROPERTY_LEVEL_LIGHT, "level", 0, 15);
    register_range_property(BLOCK_PROPERTY_MOISTURE, "moisture", 0, 7);
    register_range_property(BLOCK_PROPERTY_NOTE, "note", 0, 24);
    register_range_property(BLOCK_PROPERTY_PICKLES, "pickles", 1, 4);
    register_range_property(BLOCK_PROPERTY_POWER, "power", 0, 15);
    register_range_property(BLOCK_PROPERTY_STAGE, "stage", 0, 1);
    register_range_property(BLOCK_PROPERTY_STABILITY_DISTANCE, "distance", 0, 7);
    register_range_property(BLOCK_PROPERTY_RESPAWN_ANCHOR_CHARGES, "charges", 0, 4);
    register_range_property(BLOCK_PROPERTY_ROTATION_16, "rotation", 0, 15);
    register_property_v(BLOCK_PROPERTY_BED_PART, "part", 2, "head", "foot");
    register_property_v(BLOCK_PROPERTY_CHEST_TYPE, "type", 3, "single", "left", "right");
    register_property_v(BLOCK_PROPERTY_MODE_COMPARATOR, "mode", 2, "compare", "subtract");
    register_property_v(BLOCK_PROPERTY_DOOR_HINGE, "hinge", 2, "left", "right");
    register_property_v(BLOCK_PROPERTY_NOTEBLOCK_INSTRUMENT, "instrument", 16, "harp", "basedrum", "snare", "hat", "bass", "flute", "bell", "guitar", "chime", "xylophone", "iron_xylophone", "cow_bell", "didgeridoo", "bit", "banjo", "pling");
    register_property_v(BLOCK_PROPERTY_PISTON_TYPE, "type", 2, "normal", "sticky");
    register_property_v(BLOCK_PROPERTY_SLAB_TYPE, "type", 3, "top", "bottom", "double");
    register_property_v(BLOCK_PROPERTY_STAIRS_SHAPE, "shape", 5, "straight", "inner_left", "inner_right", "outer_left", "outer_right");
    register_property_v(BLOCK_PROPERTY_STRUCTUREBLOCK_MODE, "mode", 4, "save", "load", "corner", "data");
    register_property_v(BLOCK_PROPERTY_BAMBOO_LEAVES, "leaves", 3, "none", "small", "large");
    register_property_v(BLOCK_PROPERTY_DRIPLEAF_TILT, "tilt", 4, "none", "unstable", "partial", "full");
    register_property_v(BLOCK_PROPERTY_VERTICAL_DIRECTION, "vertical_direction", 2, "up", "down");
    register_property_v(BLOCK_PROPERTY_DRIPSTONE_THICKNESS, "thickness", 5, "tip_merge", "tip", "frustum", "middle", "base");
    register_property_v(BLOCK_PROPERTY_SCULK_SENSOR_PHASE, "sculk_sensor_phase", 3, "inactive", "active", "cooldown");

    block_properties * props;
    i32 block_type;

    InitSimpleEmptyBlock("minecraft:air");
    InitSimpleFullBlock("minecraft:stone");
    InitSimpleFullBlock("minecraft:granite");
    InitSimpleFullBlock("minecraft:polished_granite");
    InitSimpleFullBlock("minecraft:diorite");
    InitSimpleFullBlock("minecraft:polished_diorite");
    InitSimpleFullBlock("minecraft:andesite");
    InitSimpleFullBlock("minecraft:polished_andesite");
    init_snowy_grassy_block("minecraft:grass_block");
    InitSimpleFullBlock("minecraft:dirt");
    InitSimpleFullBlock("minecraft:coarse_dirt");
    init_snowy_grassy_block("minecraft:podzol");
    InitSimpleFullBlock("minecraft:cobblestone");
    InitSimpleFullBlock("minecraft:oak_planks");
    InitSimpleFullBlock("minecraft:spruce_planks");
    InitSimpleFullBlock("minecraft:birch_planks");
    InitSimpleFullBlock("minecraft:jungle_planks");
    InitSimpleFullBlock("minecraft:acacia_planks");
    InitSimpleFullBlock("minecraft:dark_oak_planks");
    InitSimpleFullBlock("minecraft:mangrove_planks");
    init_sapling("minecraft:oak_sapling");
    init_sapling("minecraft:spruce_sapling");
    init_sapling("minecraft:birch_sapling");
    init_sapling("minecraft:jungle_sapling");
    init_sapling("minecraft:acacia_sapling");
    init_sapling("minecraft:dark_oak_sapling");
    init_propagule("minecraft:mangrove_propagule");
    InitSimpleFullBlock("minecraft:bedrock");

    // @TODO(traks) slower movement in fluids
    block_type = register_block_type("minecraft:water");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LEVEL, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 1);

    // @TODO(traks) slower movement in fluids
    block_type = register_block_type("minecraft:lava");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LEVEL, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 1);

    InitSimpleFullBlock("minecraft:sand");
    InitSimpleFullBlock("minecraft:red_sand");
    InitSimpleFullBlock("minecraft:gravel");
    InitSimpleFullBlock("minecraft:gold_ore");
    InitSimpleFullBlock("minecraft:deepslate_gold_ore");
    InitSimpleFullBlock("minecraft:iron_ore");
    InitSimpleFullBlock("minecraft:deepslate_iron_ore");
    InitSimpleFullBlock("minecraft:coal_ore");
    InitSimpleFullBlock("minecraft:deepslate_coal_ore");
    InitSimpleFullBlock("minecraft:nether_gold_ore");
    init_pillar("minecraft:oak_log");
    init_pillar("minecraft:spruce_log");
    init_pillar("minecraft:birch_log");
    init_pillar("minecraft:jungle_log");
    init_pillar("minecraft:acacia_log");
    init_pillar("minecraft:dark_oak_log");
    init_pillar("minecraft:mangrove_log");

    block_type = register_block_type("minecraft:mangrove_roots");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionAuto(props);

    init_pillar("minecraft:muddy_mangrove_roots");
    init_pillar("minecraft:stripped_spruce_log");
    init_pillar("minecraft:stripped_birch_log");
    init_pillar("minecraft:stripped_jungle_log");
    init_pillar("minecraft:stripped_acacia_log");
    init_pillar("minecraft:stripped_dark_oak_log");
    init_pillar("minecraft:stripped_oak_log");
    init_pillar("minecraft:stripped_mangrove_log");
    init_pillar("minecraft:oak_wood");
    init_pillar("minecraft:spruce_wood");
    init_pillar("minecraft:birch_wood");
    init_pillar("minecraft:jungle_wood");
    init_pillar("minecraft:acacia_wood");
    init_pillar("minecraft:dark_oak_wood");
    init_pillar("minecraft:mangrove_wood");
    init_pillar("minecraft:stripped_oak_wood");
    init_pillar("minecraft:stripped_spruce_wood");
    init_pillar("minecraft:stripped_birch_wood");
    init_pillar("minecraft:stripped_jungle_wood");
    init_pillar("minecraft:stripped_acacia_wood");
    init_pillar("minecraft:stripped_dark_oak_wood");
    init_pillar("minecraft:stripped_mangrove_wood");
    init_leaves("minecraft:oak_leaves");
    init_leaves("minecraft:spruce_leaves");
    init_leaves("minecraft:birch_leaves");
    init_leaves("minecraft:jungle_leaves");
    init_leaves("minecraft:acacia_leaves");
    init_leaves("minecraft:dark_oak_leaves");
    init_leaves("minecraft:mangrove_leaves");
    init_leaves("minecraft:azalea_leaves");
    init_leaves("minecraft:flowering_azalea_leaves");
    InitSimpleFullBlock("minecraft:sponge");
    InitSimpleFullBlock("minecraft:wet_sponge");
    InitSimpleFullBlock("minecraft:glass");
    InitSimpleFullBlock("minecraft:lapis_ore");
    InitSimpleFullBlock("minecraft:deepslate_lapis_ore");
    InitSimpleFullBlock("minecraft:lapis_block");

    block_type = register_block_type("minecraft:dispenser");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_TRIGGERED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    InitSimpleFullBlock("minecraft:sandstone");
    InitSimpleFullBlock("minecraft:chiseled_sandstone");
    InitSimpleFullBlock("minecraft:cut_sandstone");

    block_type = register_block_type("minecraft:note_block");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_NOTEBLOCK_INSTRUMENT, "harp");
    add_block_property(props, BLOCK_PROPERTY_NOTE, "0");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    init_bed("minecraft:white_bed");
    init_bed("minecraft:orange_bed");
    init_bed("minecraft:magenta_bed");
    init_bed("minecraft:light_blue_bed");
    init_bed("minecraft:yellow_bed");
    init_bed("minecraft:lime_bed");
    init_bed("minecraft:pink_bed");
    init_bed("minecraft:gray_bed");
    init_bed("minecraft:light_gray_bed");
    init_bed("minecraft:cyan_bed");
    init_bed("minecraft:purple_bed");
    init_bed("minecraft:blue_bed");
    init_bed("minecraft:brown_bed");
    init_bed("minecraft:green_bed");
    init_bed("minecraft:red_bed");
    init_bed("minecraft:black_bed");

    block_type = register_block_type("minecraft:powered_rail");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_RAIL_SHAPE_STRAIGHT, "north_south");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:detector_rail");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_RAIL_SHAPE_STRAIGHT, "north_south");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:sticky_piston");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_EXTENDED, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    finalise_block_props(props);

    // @TODO(traks) slow down entities in cobwebs
    init_simple_block("minecraft:cobweb", BLOCK_MODEL_EMPTY, 1);
    InitSimpleEmptyBlock("minecraft:grass");
    InitSimpleEmptyBlock("minecraft:fern");
    InitSimpleEmptyBlock("minecraft:dead_bush");
    init_simple_block("minecraft:seagrass", BLOCK_MODEL_EMPTY, 1);

    init_tall_plant("minecraft:tall_seagrass", 1);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:piston");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_EXTENDED, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    finalise_block_props(props);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:piston_head");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_SHORT_PISTON, "false");
    add_block_property(props, BLOCK_PROPERTY_PISTON_TYPE, "normal");
    finalise_block_props(props);

    InitSimpleFullBlock("minecraft:white_wool");
    InitSimpleFullBlock("minecraft:orange_wool");
    InitSimpleFullBlock("minecraft:magenta_wool");
    InitSimpleFullBlock("minecraft:light_blue_wool");
    InitSimpleFullBlock("minecraft:yellow_wool");
    InitSimpleFullBlock("minecraft:lime_wool");
    InitSimpleFullBlock("minecraft:pink_wool");
    InitSimpleFullBlock("minecraft:gray_wool");
    InitSimpleFullBlock("minecraft:light_gray_wool");
    InitSimpleFullBlock("minecraft:cyan_wool");
    InitSimpleFullBlock("minecraft:purple_wool");
    InitSimpleFullBlock("minecraft:blue_wool");
    InitSimpleFullBlock("minecraft:brown_wool");
    InitSimpleFullBlock("minecraft:green_wool");
    InitSimpleFullBlock("minecraft:red_wool");
    InitSimpleFullBlock("minecraft:black_wool");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:moving_piston");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_PISTON_TYPE, "normal");
    finalise_block_props(props);

    InitSimpleEmptyBlock("minecraft:dandelion");
    InitSimpleEmptyBlock("minecraft:poppy");
    InitSimpleEmptyBlock("minecraft:blue_orchid");
    InitSimpleEmptyBlock("minecraft:allium");
    InitSimpleEmptyBlock("minecraft:azure_bluet");
    InitSimpleEmptyBlock("minecraft:red_tulip");
    InitSimpleEmptyBlock("minecraft:orange_tulip");
    InitSimpleEmptyBlock("minecraft:white_tulip");
    InitSimpleEmptyBlock("minecraft:pink_tulip");
    InitSimpleEmptyBlock("minecraft:oxeye_daisy");
    InitSimpleEmptyBlock("minecraft:cornflower");
    InitSimpleEmptyBlock("minecraft:wither_rose");
    InitSimpleEmptyBlock("minecraft:lily_of_the_valley");
    InitSimpleEmptyBlock("minecraft:brown_mushroom");
    InitSimpleEmptyBlock("minecraft:red_mushroom");
    InitSimpleFullBlock("minecraft:gold_block");
    InitSimpleFullBlock("minecraft:iron_block");
    InitSimpleFullBlock("minecraft:bricks");

    block_type = register_block_type("minecraft:tnt");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_UNSTABLE, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    InitSimpleFullBlock("minecraft:bookshelf");
    InitSimpleFullBlock("minecraft:mossy_cobblestone");
    InitSimpleFullBlock("minecraft:obsidian");
    init_simple_block("minecraft:torch", BLOCK_MODEL_EMPTY, 0);

    block_type = register_block_type("minecraft:wall_torch");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:fire");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_15, "0");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    // @TODO(traks) do damage in fire
    init_simple_block("minecraft:soul_fire", BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:spawner", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 1);

    // TODO(traks): block models + light reduction
    init_stair_props("minecraft:oak_stairs");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:chest");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_CHEST_TYPE, "single");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:redstone_wire");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_REDSTONE_POS_X, "none");
    add_block_property(props, BLOCK_PROPERTY_REDSTONE_NEG_Z, "none");
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    add_block_property(props, BLOCK_PROPERTY_REDSTONE_POS_Z, "none");
    add_block_property(props, BLOCK_PROPERTY_REDSTONE_NEG_X, "none");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitSimpleFullBlock("minecraft:diamond_ore");
    InitSimpleFullBlock("minecraft:deepslate_diamond_ore");
    InitSimpleFullBlock("minecraft:diamond_block");
    InitSimpleFullBlock("minecraft:crafting_table");

    block_type = register_block_type("minecraft:wheat");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:farmland");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_MOISTURE, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_Y_15);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:furnace");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    init_sign("minecraft:oak_sign");
    init_sign("minecraft:spruce_sign");
    init_sign("minecraft:birch_sign");
    init_sign("minecraft:acacia_sign");
    init_sign("minecraft:jungle_sign");
    init_sign("minecraft:dark_oak_sign");
    init_sign("minecraft:mangrove_sign");

    // TODO(traks): block models + light reduction
    init_door_props("minecraft:oak_door");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:ladder");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:rail");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_RAIL_SHAPE, "north_south");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    init_stair_props("minecraft:cobblestone_stairs");

    init_wall_sign("minecraft:oak_wall_sign");
    init_wall_sign("minecraft:spruce_wall_sign");
    init_wall_sign("minecraft:birch_wall_sign");
    init_wall_sign("minecraft:acacia_wall_sign");
    init_wall_sign("minecraft:jungle_wall_sign");
    init_wall_sign("minecraft:dark_oak_wall_sign");
    init_wall_sign("minecraft:mangrove_wall_sign");

    block_type = register_block_type("minecraft:lever");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ATTACH_FACE, "wall");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    init_pressure_plate("minecraft:stone_pressure_plate");

    init_door_props("minecraft:iron_door");

    init_pressure_plate("minecraft:oak_pressure_plate");
    init_pressure_plate("minecraft:spruce_pressure_plate");
    init_pressure_plate("minecraft:birch_pressure_plate");
    init_pressure_plate("minecraft:jungle_pressure_plate");
    init_pressure_plate("minecraft:acacia_pressure_plate");
    init_pressure_plate("minecraft:dark_oak_pressure_plate");
    init_pressure_plate("minecraft:mangrove_pressure_plate");

    init_redstone_ore("minecraft:redstone_ore");
    init_redstone_ore("minecraft:deepslate_redstone_ore");

    block_type = register_block_type("minecraft:redstone_torch");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LIT, "true");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:redstone_wall_torch");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "true");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    init_button("minecraft:stone_button");

    block_type = register_block_type("minecraft:snow");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LAYERS, "1");
    finalise_block_props(props);
    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        i32 collisionModelId = BLOCK_MODEL_EMPTY + (info.layers - 1) * 2;
        i32 supportModelId = BLOCK_MODEL_EMPTY + info.layers * 2;
        serv->collisionModelByState[block_state] = collisionModelId;
        serv->supportModelByState[block_state] = supportModelId;
        serv->lightBlockingModelByState[block_state] = supportModelId;
    }
    SetLightReductionForAllStates(props, 0);

    InitSimpleBlockWithModels("minecraft:ice", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 1);
    InitSimpleFullBlock("minecraft:snow_block");

    block_type = register_block_type("minecraft:cactus");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_15, "0");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_CACTUS, BLOCK_MODEL_CACTUS, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitSimpleFullBlock("minecraft:clay");

    block_type = register_block_type("minecraft:sugar_cane");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_15, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:jukebox");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HAS_RECORD, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    init_fence("minecraft:oak_fence", 1);

    InitSimpleFullBlock("minecraft:pumpkin");
    InitSimpleFullBlock("minecraft:netherrack");
    InitSimpleBlockWithModels("minecraft:soul_sand", BLOCK_MODEL_Y_14, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, 15);
    InitSimpleFullBlock("minecraft:soul_soil");
    init_pillar("minecraft:basalt");
    init_pillar("minecraft:polished_basalt");
    init_simple_block("minecraft:soul_torch", BLOCK_MODEL_EMPTY, 0);

    block_type = register_block_type("minecraft:soul_wall_torch");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    init_simple_block("minecraft:glowstone", BLOCK_MODEL_FULL, 15);

    block_type = register_block_type("minecraft:nether_portal");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_AXIS, "x");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:carved_pumpkin");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    block_type = register_block_type("minecraft:jack_o_lantern");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:cake");
    props = serv->block_properties_table + BLOCK_CAKE;
    add_block_property(props, BLOCK_PROPERTY_BITES, "0");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:repeater");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_DELAY, "1");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LOCKED, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_Y_2, BLOCK_MODEL_Y_2, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitSimpleBlockWithModels("minecraft:white_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:orange_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:magenta_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:light_blue_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:yellow_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:lime_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:pink_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:gray_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:light_gray_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:cyan_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:purple_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:blue_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:brown_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:green_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:red_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:black_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);

    // @TODO(traks) collision models
    init_trapdoor_props("minecraft:oak_trapdoor");
    init_trapdoor_props("minecraft:spruce_trapdoor");
    init_trapdoor_props("minecraft:birch_trapdoor");
    init_trapdoor_props("minecraft:jungle_trapdoor");
    init_trapdoor_props("minecraft:acacia_trapdoor");
    init_trapdoor_props("minecraft:dark_oak_trapdoor");
    init_trapdoor_props("minecraft:mangrove_trapdoor");

    InitSimpleFullBlock("minecraft:stone_bricks");
    InitSimpleFullBlock("minecraft:mossy_stone_bricks");
    InitSimpleFullBlock("minecraft:cracked_stone_bricks");
    InitSimpleFullBlock("minecraft:chiseled_stone_bricks");

    InitSimpleFullBlock("minecraft:packed_mud");
    InitSimpleFullBlock("minecraft:mud_bricks");

    InitSimpleFullBlock("minecraft:infested_stone");
    InitSimpleFullBlock("minecraft:infested_cobblestone");
    InitSimpleFullBlock("minecraft:infested_stone_bricks");
    InitSimpleFullBlock("minecraft:infested_mossy_stone_bricks");
    InitSimpleFullBlock("minecraft:infested_cracked_stone_bricks");
    InitSimpleFullBlock("minecraft:infested_chiseled_stone_bricks");
    init_mushroom_block("minecraft:brown_mushroom_block");
    init_mushroom_block("minecraft:red_mushroom_block");
    init_mushroom_block("minecraft:mushroom_stem");

    init_pane("minecraft:iron_bars");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:chain");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AXIS, "y");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    init_pane("minecraft:glass_pane");

    InitSimpleFullBlock("minecraft:melon");

    block_type = register_block_type("minecraft:attached_pumpkin_stem");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:attached_melon_stem");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:pumpkin_stem");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:melon_stem");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:vine");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitMultiFaceBlock("minecraft:glow_lichen");

    init_fence_gate("minecraft:oak_fence_gate");

    init_stair_props("minecraft:brick_stairs");
    init_stair_props("minecraft:stone_brick_stairs");
    init_stair_props("minecraft:mud_brick_stairs");

    init_snowy_grassy_block("minecraft:mycelium");

    InitSimpleBlockWithModels("minecraft:lily_pad", BLOCK_MODEL_LILY_PAD, BLOCK_MODEL_LILY_PAD, BLOCK_MODEL_EMPTY, 0);
    InitSimpleFullBlock("minecraft:nether_bricks");

    init_fence("minecraft:nether_brick_fence", 0);

    init_stair_props("minecraft:nether_brick_stairs");

    block_type = register_block_type("minecraft:nether_wart");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_3, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitSimpleBlockWithModels("minecraft:enchanting_table", BLOCK_MODEL_Y_12, BLOCK_MODEL_Y_12, BLOCK_MODEL_Y_12, 1);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:brewing_stand");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HAS_BOTTLE_0, "false");
    add_block_property(props, BLOCK_PROPERTY_HAS_BOTTLE_1, "false");
    add_block_property(props, BLOCK_PROPERTY_HAS_BOTTLE_2, "false");
    finalise_block_props(props);

    init_cauldron("minecraft:cauldron", 0);
    init_cauldron("minecraft:water_cauldron", 1);
    init_cauldron("minecraft:lava_cauldron", 0);
    init_cauldron("minecraft:powder_snow_cauldron", 1);

    InitSimpleEmptyBlock("minecraft:end_portal");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:end_portal_frame");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_EYE, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);

    InitSimpleFullBlock("minecraft:end_stone");
    // @TODO(traks) correct block model
    InitSimpleBlockWithModels("minecraft:dragon_egg", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);

    block_type = register_block_type("minecraft:redstone_lamp");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:cocoa");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_2, "0");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);

    init_stair_props("minecraft:sandstone_stairs");

    InitSimpleFullBlock("minecraft:emerald_ore");
    InitSimpleFullBlock("minecraft:deepslate_emerald_ore");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:ender_chest");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:tripwire_hook");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ATTACHED, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:tripwire");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ATTACHED, "false");
    add_block_property(props, BLOCK_PROPERTY_DISARMED, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitSimpleFullBlock("minecraft:emerald_block");

    init_stair_props("minecraft:spruce_stairs");
    init_stair_props("minecraft:birch_stairs");
    init_stair_props("minecraft:jungle_stairs");

    block_type = register_block_type("minecraft:command_block");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_CONDITIONAL, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    InitSimpleBlockWithModels("minecraft:beacon", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 1);

    // @TODO(traks) collision models
    init_wall_props("minecraft:cobblestone_wall");
    init_wall_props("minecraft:mossy_cobblestone_wall");

    InitFlowerPot("minecraft:flower_pot");
    InitFlowerPot("minecraft:potted_oak_sapling");
    InitFlowerPot("minecraft:potted_spruce_sapling");
    InitFlowerPot("minecraft:potted_birch_sapling");
    InitFlowerPot("minecraft:potted_jungle_sapling");
    InitFlowerPot("minecraft:potted_acacia_sapling");
    InitFlowerPot("minecraft:potted_dark_oak_sapling");
    InitFlowerPot("minecraft:potted_mangrove_propagule");
    InitFlowerPot("minecraft:potted_fern");
    InitFlowerPot("minecraft:potted_dandelion");
    InitFlowerPot("minecraft:potted_poppy");
    InitFlowerPot("minecraft:potted_blue_orchid");
    InitFlowerPot("minecraft:potted_allium");
    InitFlowerPot("minecraft:potted_azure_bluet");
    InitFlowerPot("minecraft:potted_red_tulip");
    InitFlowerPot("minecraft:potted_orange_tulip");
    InitFlowerPot("minecraft:potted_white_tulip");
    InitFlowerPot("minecraft:potted_pink_tulip");
    InitFlowerPot("minecraft:potted_oxeye_daisy");
    InitFlowerPot("minecraft:potted_cornflower");
    InitFlowerPot("minecraft:potted_lily_of_the_valley");
    InitFlowerPot("minecraft:potted_wither_rose");
    InitFlowerPot("minecraft:potted_red_mushroom");
    InitFlowerPot("minecraft:potted_brown_mushroom");
    InitFlowerPot("minecraft:potted_dead_bush");
    InitFlowerPot("minecraft:potted_cactus");

    block_type = register_block_type("minecraft:carrots");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:potatoes");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    init_button("minecraft:oak_button");
    init_button("minecraft:spruce_button");
    init_button("minecraft:birch_button");
    init_button("minecraft:jungle_button");
    init_button("minecraft:acacia_button");
    init_button("minecraft:dark_oak_button");
    init_button("minecraft:mangrove_button");

    // @TODO(traks) collision models
    init_skull_props("minecraft:skeleton_skull");
    init_wall_skull_props("minecraft:skeleton_wall_skull");
    init_skull_props("minecraft:wither_skeleton_skull");
    init_wall_skull_props("minecraft:wither_skeleton_wall_skull");
    init_skull_props("minecraft:zombie_head");
    init_wall_skull_props("minecraft:zombie_wall_head");
    init_skull_props("minecraft:player_head");
    init_wall_skull_props("minecraft:player_wall_head");
    init_skull_props("minecraft:creeper_head");
    init_wall_skull_props("minecraft:creeper_wall_head");
    init_skull_props("minecraft:dragon_head");
    init_wall_skull_props("minecraft:dragon_wall_head");

    // @TODO(traks) collision models
    init_anvil_props("minecraft:anvil");
    init_anvil_props("minecraft:chipped_anvil");
    init_anvil_props("minecraft:damaged_anvil");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:trapped_chest");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_CHEST_TYPE, "single");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:light_weighted_pressure_plate");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:heavy_weighted_pressure_plate");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:comparator");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_MODE_COMPARATOR, "compare");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_Y_2, BLOCK_MODEL_Y_2, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:daylight_detector");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_INVERTED, "false");
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_Y_6);
    SetLightReductionForAllStates(props, 0);

    InitSimpleFullBlock("minecraft:redstone_block");
    InitSimpleFullBlock("minecraft:nether_quartz_ore");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:hopper");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ENABLED, "true");
    add_block_property(props, BLOCK_PROPERTY_FACING_HOPPER, "down");
    finalise_block_props(props);

    InitSimpleFullBlock("minecraft:quartz_block");
    InitSimpleFullBlock("minecraft:chiseled_quartz_block");

    init_pillar("minecraft:quartz_pillar");

    init_stair_props("minecraft:quartz_stairs");

    block_type = register_block_type("minecraft:activator_rail");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_RAIL_SHAPE_STRAIGHT, "north_south");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:dropper");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_TRIGGERED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    InitSimpleFullBlock("minecraft:white_terracotta");
    InitSimpleFullBlock("minecraft:orange_terracotta");
    InitSimpleFullBlock("minecraft:magenta_terracotta");
    InitSimpleFullBlock("minecraft:light_blue_terracotta");
    InitSimpleFullBlock("minecraft:yellow_terracotta");
    InitSimpleFullBlock("minecraft:lime_terracotta");
    InitSimpleFullBlock("minecraft:pink_terracotta");
    InitSimpleFullBlock("minecraft:gray_terracotta");
    InitSimpleFullBlock("minecraft:light_gray_terracotta");
    InitSimpleFullBlock("minecraft:cyan_terracotta");
    InitSimpleFullBlock("minecraft:purple_terracotta");
    InitSimpleFullBlock("minecraft:blue_terracotta");
    InitSimpleFullBlock("minecraft:brown_terracotta");
    InitSimpleFullBlock("minecraft:green_terracotta");
    InitSimpleFullBlock("minecraft:red_terracotta");
    InitSimpleFullBlock("minecraft:black_terracotta");

    init_pane("minecraft:white_stained_glass_pane");
    init_pane("minecraft:orange_stained_glass_pane");
    init_pane("minecraft:magenta_stained_glass_pane");
    init_pane("minecraft:light_blue_stained_glass_pane");
    init_pane("minecraft:yellow_stained_glass_pane");
    init_pane("minecraft:lime_stained_glass_pane");
    init_pane("minecraft:pink_stained_glass_pane");
    init_pane("minecraft:gray_stained_glass_pane");
    init_pane("minecraft:light_gray_stained_glass_pane");
    init_pane("minecraft:cyan_stained_glass_pane");
    init_pane("minecraft:purple_stained_glass_pane");
    init_pane("minecraft:blue_stained_glass_pane");
    init_pane("minecraft:brown_stained_glass_pane");
    init_pane("minecraft:green_stained_glass_pane");
    init_pane("minecraft:red_stained_glass_pane");
    init_pane("minecraft:black_stained_glass_pane");

    init_stair_props("minecraft:acacia_stairs");
    init_stair_props("minecraft:dark_oak_stairs");
    init_stair_props("minecraft:mangrove_stairs");

    InitSimpleBlockWithModels("minecraft:slime_block", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 1);
    InitSimpleBlockWithModels("minecraft:barrier", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0);

    block_type = register_block_type("minecraft:light");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LEVEL_LIGHT, "15");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    init_trapdoor_props("minecraft:iron_trapdoor");

    InitSimpleFullBlock("minecraft:prismarine");
    InitSimpleFullBlock("minecraft:prismarine_bricks");
    InitSimpleFullBlock("minecraft:dark_prismarine");

    init_stair_props("minecraft:prismarine_stairs");
    init_stair_props("minecraft:prismarine_brick_stairs");
    init_stair_props("minecraft:dark_prismarine_stairs");

    init_slab("minecraft:prismarine_slab");
    init_slab("minecraft:prismarine_brick_slab");
    init_slab("minecraft:dark_prismarine_slab");

    init_simple_block("minecraft:sea_lantern", BLOCK_MODEL_FULL, 15);

    init_pillar("minecraft:hay_block");

    InitCarpet("minecraft:white_carpet");
    InitCarpet("minecraft:orange_carpet");
    InitCarpet("minecraft:magenta_carpet");
    InitCarpet("minecraft:light_blue_carpet");
    InitCarpet("minecraft:yellow_carpet");
    InitCarpet("minecraft:lime_carpet");
    InitCarpet("minecraft:pink_carpet");
    InitCarpet("minecraft:gray_carpet");
    InitCarpet("minecraft:light_gray_carpet");
    InitCarpet("minecraft:cyan_carpet");
    InitCarpet("minecraft:purple_carpet");
    InitCarpet("minecraft:blue_carpet");
    InitCarpet("minecraft:brown_carpet");
    InitCarpet("minecraft:green_carpet");
    InitCarpet("minecraft:red_carpet");
    InitCarpet("minecraft:black_carpet");
    InitSimpleFullBlock("minecraft:terracotta");
    InitSimpleFullBlock("minecraft:coal_block");
    InitSimpleFullBlock("minecraft:packed_ice");

    init_tall_plant("minecraft:sunflower", 0);
    init_tall_plant("minecraft:lilac", 0);
    init_tall_plant("minecraft:rose_bush", 0);
    init_tall_plant("minecraft:peony", 0);
    init_tall_plant("minecraft:tall_grass", 0);
    init_tall_plant("minecraft:large_fern", 0);

    init_banner("minecraft:white_banner");
    init_banner("minecraft:orange_banner");
    init_banner("minecraft:magenta_banner");
    init_banner("minecraft:light_blue_banner");
    init_banner("minecraft:yellow_banner");
    init_banner("minecraft:lime_banner");
    init_banner("minecraft:pink_banner");
    init_banner("minecraft:gray_banner");
    init_banner("minecraft:light_gray_banner");
    init_banner("minecraft:cyan_banner");
    init_banner("minecraft:purple_banner");
    init_banner("minecraft:blue_banner");
    init_banner("minecraft:brown_banner");
    init_banner("minecraft:green_banner");
    init_banner("minecraft:red_banner");
    init_banner("minecraft:black_banner");

    init_wall_banner("minecraft:white_wall_banner");
    init_wall_banner("minecraft:orange_wall_banner");
    init_wall_banner("minecraft:magenta_wall_banner");
    init_wall_banner("minecraft:light_blue_wall_banner");
    init_wall_banner("minecraft:yellow_wall_banner");
    init_wall_banner("minecraft:lime_wall_banner");
    init_wall_banner("minecraft:pink_wall_banner");
    init_wall_banner("minecraft:gray_wall_banner");
    init_wall_banner("minecraft:light_gray_wall_banner");
    init_wall_banner("minecraft:cyan_wall_banner");
    init_wall_banner("minecraft:purple_wall_banner");
    init_wall_banner("minecraft:blue_wall_banner");
    init_wall_banner("minecraft:brown_wall_banner");
    init_wall_banner("minecraft:green_wall_banner");
    init_wall_banner("minecraft:red_wall_banner");
    init_wall_banner("minecraft:black_wall_banner");

    InitSimpleFullBlock("minecraft:red_sandstone");
    InitSimpleFullBlock("minecraft:chiseled_red_sandstone");
    InitSimpleFullBlock("minecraft:cut_red_sandstone");

    init_stair_props("minecraft:red_sandstone_stairs");

    init_slab("minecraft:oak_slab");
    init_slab("minecraft:spruce_slab");
    init_slab("minecraft:birch_slab");
    init_slab("minecraft:jungle_slab");
    init_slab("minecraft:acacia_slab");
    init_slab("minecraft:dark_oak_slab");
    init_slab("minecraft:mangrove_slab");
    init_slab("minecraft:stone_slab");
    init_slab("minecraft:smooth_stone_slab");
    init_slab("minecraft:sandstone_slab");
    init_slab("minecraft:cut_sandstone_slab");
    init_slab("minecraft:petrified_oak_slab");
    init_slab("minecraft:cobblestone_slab");
    init_slab("minecraft:brick_slab");
    init_slab("minecraft:stone_brick_slab");
    init_slab("minecraft:mud_brick_slab");
    init_slab("minecraft:nether_brick_slab");
    init_slab("minecraft:quartz_slab");
    init_slab("minecraft:red_sandstone_slab");
    init_slab("minecraft:cut_red_sandstone_slab");
    init_slab("minecraft:purpur_slab");

    InitSimpleFullBlock("minecraft:smooth_stone");
    InitSimpleFullBlock("minecraft:smooth_sandstone");
    InitSimpleFullBlock("minecraft:smooth_quartz");
    InitSimpleFullBlock("minecraft:smooth_red_sandstone");

    init_fence_gate("minecraft:spruce_fence_gate");
    init_fence_gate("minecraft:birch_fence_gate");
    init_fence_gate("minecraft:jungle_fence_gate");
    init_fence_gate("minecraft:acacia_fence_gate");
    init_fence_gate("minecraft:dark_oak_fence_gate");
    init_fence_gate("minecraft:mangrove_fence_gate");

    init_fence("minecraft:spruce_fence", 1);
    init_fence("minecraft:birch_fence", 1);
    init_fence("minecraft:jungle_fence", 1);
    init_fence("minecraft:acacia_fence", 1);
    init_fence("minecraft:dark_oak_fence", 1);
    init_fence("minecraft:mangrove_fence", 1);

    init_door_props("minecraft:spruce_door");
    init_door_props("minecraft:birch_door");
    init_door_props("minecraft:jungle_door");
    init_door_props("minecraft:acacia_door");
    init_door_props("minecraft:dark_oak_door");
    init_door_props("minecraft:mangrove_door");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:end_rod");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "up");
    finalise_block_props(props);

    // TODO(traks): block modesl + light reduction
    block_type = register_block_type("minecraft:chorus_plant");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_NEG_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    finalise_block_props(props);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:chorus_flower");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_5, "0");
    finalise_block_props(props);

    InitSimpleFullBlock("minecraft:purpur_block");

    init_pillar("minecraft:purpur_pillar");

    init_stair_props("minecraft:purpur_stairs");

    InitSimpleFullBlock("minecraft:end_stone_bricks");

    block_type = register_block_type("minecraft:beetroots");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_3, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    init_simple_block("minecraft:dirt_path", BLOCK_MODEL_Y_15, 0);
    InitSimpleEmptyBlock("minecraft:end_gateway");

    block_type = register_block_type("minecraft:repeating_command_block");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_CONDITIONAL, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    block_type = register_block_type("minecraft:chain_command_block");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_CONDITIONAL, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    block_type = register_block_type("minecraft:frosted_ice");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_3, "0");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    InitSimpleFullBlock("minecraft:magma_block");
    InitSimpleFullBlock("minecraft:nether_wart_block");
    InitSimpleFullBlock("minecraft:red_nether_bricks");

    init_pillar("minecraft:bone_block");

    InitSimpleEmptyBlock("minecraft:structure_void");

    block_type = register_block_type("minecraft:observer");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "south");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    // @TODO(traks) collision models
    init_shulker_box_props("minecraft:shulker_box");
    init_shulker_box_props("minecraft:white_shulker_box");
    init_shulker_box_props("minecraft:orange_shulker_box");
    init_shulker_box_props("minecraft:magenta_shulker_box");
    init_shulker_box_props("minecraft:light_blue_shulker_box");
    init_shulker_box_props("minecraft:yellow_shulker_box");
    init_shulker_box_props("minecraft:lime_shulker_box");
    init_shulker_box_props("minecraft:pink_shulker_box");
    init_shulker_box_props("minecraft:gray_shulker_box");
    init_shulker_box_props("minecraft:light_gray_shulker_box");
    init_shulker_box_props("minecraft:cyan_shulker_box");
    init_shulker_box_props("minecraft:purple_shulker_box");
    init_shulker_box_props("minecraft:blue_shulker_box");
    init_shulker_box_props("minecraft:brown_shulker_box");
    init_shulker_box_props("minecraft:green_shulker_box");
    init_shulker_box_props("minecraft:red_shulker_box");
    init_shulker_box_props("minecraft:black_shulker_box");

    init_glazed_terracotta("minecraft:white_glazed_terracotta");
    init_glazed_terracotta("minecraft:orange_glazed_terracotta");
    init_glazed_terracotta("minecraft:magenta_glazed_terracotta");
    init_glazed_terracotta("minecraft:light_blue_glazed_terracotta");
    init_glazed_terracotta("minecraft:yellow_glazed_terracotta");
    init_glazed_terracotta("minecraft:lime_glazed_terracotta");
    init_glazed_terracotta("minecraft:pink_glazed_terracotta");
    init_glazed_terracotta("minecraft:gray_glazed_terracotta");
    init_glazed_terracotta("minecraft:light_gray_glazed_terracotta");
    init_glazed_terracotta("minecraft:cyan_glazed_terracotta");
    init_glazed_terracotta("minecraft:purple_glazed_terracotta");
    init_glazed_terracotta("minecraft:blue_glazed_terracotta");
    init_glazed_terracotta("minecraft:brown_glazed_terracotta");
    init_glazed_terracotta("minecraft:green_glazed_terracotta");
    init_glazed_terracotta("minecraft:red_glazed_terracotta");
    init_glazed_terracotta("minecraft:black_glazed_terracotta");

    InitSimpleFullBlock("minecraft:white_concrete");
    InitSimpleFullBlock("minecraft:orange_concrete");
    InitSimpleFullBlock("minecraft:magenta_concrete");
    InitSimpleFullBlock("minecraft:light_blue_concrete");
    InitSimpleFullBlock("minecraft:yellow_concrete");
    InitSimpleFullBlock("minecraft:lime_concrete");
    InitSimpleFullBlock("minecraft:pink_concrete");
    InitSimpleFullBlock("minecraft:gray_concrete");
    InitSimpleFullBlock("minecraft:light_gray_concrete");
    InitSimpleFullBlock("minecraft:cyan_concrete");
    InitSimpleFullBlock("minecraft:purple_concrete");
    InitSimpleFullBlock("minecraft:blue_concrete");
    InitSimpleFullBlock("minecraft:brown_concrete");
    InitSimpleFullBlock("minecraft:green_concrete");
    InitSimpleFullBlock("minecraft:red_concrete");
    InitSimpleFullBlock("minecraft:black_concrete");

    InitSimpleFullBlock("minecraft:white_concrete_powder");
    InitSimpleFullBlock("minecraft:orange_concrete_powder");
    InitSimpleFullBlock("minecraft:magenta_concrete_powder");
    InitSimpleFullBlock("minecraft:light_blue_concrete_powder");
    InitSimpleFullBlock("minecraft:yellow_concrete_powder");
    InitSimpleFullBlock("minecraft:lime_concrete_powder");
    InitSimpleFullBlock("minecraft:pink_concrete_powder");
    InitSimpleFullBlock("minecraft:gray_concrete_powder");
    InitSimpleFullBlock("minecraft:light_gray_concrete_powder");
    InitSimpleFullBlock("minecraft:cyan_concrete_powder");
    InitSimpleFullBlock("minecraft:purple_concrete_powder");
    InitSimpleFullBlock("minecraft:blue_concrete_powder");
    InitSimpleFullBlock("minecraft:brown_concrete_powder");
    InitSimpleFullBlock("minecraft:green_concrete_powder");
    InitSimpleFullBlock("minecraft:red_concrete_powder");
    InitSimpleFullBlock("minecraft:black_concrete_powder");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:kelp");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_25, "0");
    finalise_block_props(props);

    init_simple_block("minecraft:kelp_plant", BLOCK_MODEL_EMPTY, 1);
    InitSimpleFullBlock("minecraft:dried_kelp_block");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:turtle_egg");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_EGGS, "1");
    add_block_property(props, BLOCK_PROPERTY_HATCH, "0");
    finalise_block_props(props);

    InitSimpleFullBlock("minecraft:dead_tube_coral_block");
    InitSimpleFullBlock("minecraft:dead_brain_coral_block");
    InitSimpleFullBlock("minecraft:dead_bubble_coral_block");
    InitSimpleFullBlock("minecraft:dead_fire_coral_block");
    InitSimpleFullBlock("minecraft:dead_horn_coral_block");

    InitSimpleFullBlock("minecraft:tube_coral_block");
    InitSimpleFullBlock("minecraft:brain_coral_block");
    InitSimpleFullBlock("minecraft:bubble_coral_block");
    InitSimpleFullBlock("minecraft:fire_coral_block");
    InitSimpleFullBlock("minecraft:horn_coral_block");

    init_coral("minecraft:dead_tube_coral");
    init_coral("minecraft:dead_brain_coral");
    init_coral("minecraft:dead_bubble_coral");
    init_coral("minecraft:dead_fire_coral");
    init_coral("minecraft:dead_horn_coral");
    init_coral("minecraft:tube_coral");
    init_coral("minecraft:brain_coral");
    init_coral("minecraft:bubble_coral");
    init_coral("minecraft:fire_coral");
    init_coral("minecraft:horn_coral");

    init_coral_fan("minecraft:dead_tube_coral_fan");
    init_coral_fan("minecraft:dead_brain_coral_fan");
    init_coral_fan("minecraft:dead_bubble_coral_fan");
    init_coral_fan("minecraft:dead_fire_coral_fan");
    init_coral_fan("minecraft:dead_horn_coral_fan");
    init_coral_fan("minecraft:tube_coral_fan");
    init_coral_fan("minecraft:brain_coral_fan");
    init_coral_fan("minecraft:bubble_coral_fan");
    init_coral_fan("minecraft:fire_coral_fan");
    init_coral_fan("minecraft:horn_coral_fan");

    init_coral_wall_fan("minecraft:dead_tube_coral_wall_fan");
    init_coral_wall_fan("minecraft:dead_brain_coral_wall_fan");
    init_coral_wall_fan("minecraft:dead_bubble_coral_wall_fan");
    init_coral_wall_fan("minecraft:dead_fire_coral_wall_fan");
    init_coral_wall_fan("minecraft:dead_horn_coral_wall_fan");
    init_coral_wall_fan("minecraft:tube_coral_wall_fan");
    init_coral_wall_fan("minecraft:brain_coral_wall_fan");
    init_coral_wall_fan("minecraft:bubble_coral_wall_fan");
    init_coral_wall_fan("minecraft:fire_coral_wall_fan");
    init_coral_wall_fan("minecraft:horn_coral_wall_fan");

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:sea_pickle");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_PICKLES, "1");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    finalise_block_props(props);

    InitSimpleFullBlock("minecraft:blue_ice");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:conduit");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    finalise_block_props(props);

    init_simple_block("minecraft:bamboo_sapling", BLOCK_MODEL_EMPTY, 0);

    block_type = register_block_type("minecraft:bamboo");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_1, "0");
    add_block_property(props, BLOCK_PROPERTY_BAMBOO_LEAVES, "none");
    add_block_property(props, BLOCK_PROPERTY_STAGE, "0");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_CENTRED_BAMBOO, BLOCK_MODEL_CENTRED_BAMBOO, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitFlowerPot("minecraft:potted_bamboo");
    InitSimpleEmptyBlock("minecraft:void_air");
    InitSimpleEmptyBlock("minecraft:cave_air");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:bubble_column");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_DRAG, "true");
    finalise_block_props(props);

    init_stair_props("minecraft:polished_granite_stairs");
    init_stair_props("minecraft:smooth_red_sandstone_stairs");
    init_stair_props("minecraft:mossy_stone_brick_stairs");
    init_stair_props("minecraft:polished_diorite_stairs");
    init_stair_props("minecraft:mossy_cobblestone_stairs");
    init_stair_props("minecraft:end_stone_brick_stairs");
    init_stair_props("minecraft:stone_stairs");
    init_stair_props("minecraft:smooth_sandstone_stairs");
    init_stair_props("minecraft:smooth_quartz_stairs");
    init_stair_props("minecraft:granite_stairs");
    init_stair_props("minecraft:andesite_stairs");
    init_stair_props("minecraft:red_nether_brick_stairs");
    init_stair_props("minecraft:polished_andesite_stairs");
    init_stair_props("minecraft:diorite_stairs");

    init_slab("minecraft:polished_granite_slab");
    init_slab("minecraft:smooth_red_sandstone_slab");
    init_slab("minecraft:mossy_stone_brick_slab");
    init_slab("minecraft:polished_diorite_slab");
    init_slab("minecraft:mossy_cobblestone_slab");
    init_slab("minecraft:end_stone_brick_slab");
    init_slab("minecraft:smooth_sandstone_slab");
    init_slab("minecraft:smooth_quartz_slab");
    init_slab("minecraft:granite_slab");
    init_slab("minecraft:andesite_slab");
    init_slab("minecraft:red_nether_brick_slab");
    init_slab("minecraft:polished_andesite_slab");
    init_slab("minecraft:diorite_slab");

    init_wall_props("minecraft:brick_wall");
    init_wall_props("minecraft:prismarine_wall");
    init_wall_props("minecraft:red_sandstone_wall");
    init_wall_props("minecraft:mossy_stone_brick_wall");
    init_wall_props("minecraft:granite_wall");
    init_wall_props("minecraft:stone_brick_wall");
    init_wall_props("minecraft:mud_brick_wall");
    init_wall_props("minecraft:nether_brick_wall");
    init_wall_props("minecraft:andesite_wall");
    init_wall_props("minecraft:red_nether_brick_wall");
    init_wall_props("minecraft:sandstone_wall");
    init_wall_props("minecraft:end_stone_brick_wall");
    init_wall_props("minecraft:diorite_wall");

    // TODO(traks): block models (collision models & support model
    // (THE scaffolding model)) + light reduction
    block_type = register_block_type("minecraft:scaffolding");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_BOTTOM, "false");
    add_block_property(props, BLOCK_PROPERTY_STABILITY_DISTANCE, "7");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:loom");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    block_type = register_block_type("minecraft:barrel");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_OPEN, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    block_type = register_block_type("minecraft:smoker");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    block_type = register_block_type("minecraft:blast_furnace");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    InitSimpleFullBlock("minecraft:cartography_table");
    InitSimpleFullBlock("minecraft:fletching_table");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:grindstone");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ATTACH_FACE, "wall");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:lectern");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_HAS_BOOK, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_LECTERN);
    SetLightReductionForAllStates(props, 0);

    InitSimpleFullBlock("minecraft:smithing_table");

    block_type = register_block_type("minecraft:stonecutter");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_Y_9, BLOCK_MODEL_Y_9, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    // TODO(traks): collisions models + light reduction
    block_type = register_block_type("minecraft:bell");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_BELL_ATTACHMENT, "floor");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:lantern");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HANGING, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:soul_lantern");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HANGING, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:campfire");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "true");
    add_block_property(props, BLOCK_PROPERTY_SIGNAL_FIRE, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_Y_7, BLOCK_MODEL_Y_7, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:soul_campfire");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "true");
    add_block_property(props, BLOCK_PROPERTY_SIGNAL_FIRE, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_Y_7, BLOCK_MODEL_Y_7, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:sweet_berry_bush");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_3, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    init_pillar("minecraft:warped_stem");
    init_pillar("minecraft:stripped_warped_stem");
    init_pillar("minecraft:warped_hyphae");
    init_pillar("minecraft:stripped_warped_hyphae");

    InitSimpleFullBlock("minecraft:warped_nylium");
    InitSimpleFullBlock("minecraft:warped_fungus");
    InitSimpleFullBlock("minecraft:warped_wart_block");
    InitSimpleEmptyBlock("minecraft:warped_roots");
    InitSimpleEmptyBlock("minecraft:nether_sprouts");

    init_pillar("minecraft:crimson_stem");
    init_pillar("minecraft:stripped_crimson_stem");
    init_pillar("minecraft:crimson_hyphae");
    init_pillar("minecraft:stripped_crimson_hyphae");

    InitSimpleFullBlock("minecraft:crimson_nylium");
    InitSimpleEmptyBlock("minecraft:crimson_fungus");
    InitSimpleFullBlock("minecraft:shroomlight");

    block_type = register_block_type("minecraft:weeping_vines");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_25, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitSimpleEmptyBlock("minecraft:weeping_vines_plant");

    block_type = register_block_type("minecraft:twisting_vines");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_25, "0");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitSimpleEmptyBlock("minecraft:twisting_vines_plant");

    InitSimpleEmptyBlock("minecraft:crimson_roots");
    InitSimpleFullBlock("minecraft:crimson_planks");
    InitSimpleFullBlock("minecraft:warped_planks");

    init_slab("minecraft:crimson_slab");
    init_slab("minecraft:warped_slab");

    init_pressure_plate("minecraft:crimson_pressure_plate");
    init_pressure_plate("minecraft:warped_pressure_plate");

    init_fence("minecraft:crimson_fence", 1);
    init_fence("minecraft:warped_fence", 1);

    init_trapdoor_props("minecraft:crimson_trapdoor");
    init_trapdoor_props("minecraft:warped_trapdoor");

    init_fence_gate("minecraft:crimson_fence_gate");
    init_fence_gate("minecraft:warped_fence_gate");

    init_stair_props("minecraft:crimson_stairs");
    init_stair_props("minecraft:warped_stairs");

    init_button("minecraft:crimson_button");
    init_button("minecraft:warped_button");

    init_door_props("minecraft:crimson_door");
    init_door_props("minecraft:warped_door");

    init_sign("minecraft:crimson_sign");
    init_sign("minecraft:warped_sign");

    init_wall_sign("minecraft:crimson_wall_sign");
    init_wall_sign("minecraft:warped_wall_sign");

    block_type = register_block_type("minecraft:structure_block");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_STRUCTUREBLOCK_MODE, "save");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    block_type = register_block_type("minecraft:jigsaw");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_JIGSAW_ORIENTATION, "north_up");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    block_type = register_block_type("minecraft:composter");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LEVEL_COMPOSTER, "0");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_COMPOSTER, BLOCK_MODEL_COMPOSTER, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    block_type = register_block_type("minecraft:target");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    block_type = register_block_type("minecraft:bee_nest");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LEVEL_HONEY, "0");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    block_type = register_block_type("minecraft:beehive");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LEVEL_HONEY, "0");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    InitSimpleBlockWithModels("minecraft:honey_block", BLOCK_MODEL_HONEY_BLOCK, BLOCK_MODEL_HONEY_BLOCK, BLOCK_MODEL_EMPTY, 1);
    InitSimpleFullBlock("minecraft:honeycomb_block");
    InitSimpleFullBlock("minecraft:netherite_block");
    InitSimpleFullBlock("minecraft:ancient_debris");
    InitSimpleFullBlock("minecraft:crying_obsidian");

    block_type = register_block_type("minecraft:respawn_anchor");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_RESPAWN_ANCHOR_CHARGES, "0");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    InitFlowerPot("minecraft:potted_crimson_fungus");
    InitFlowerPot("minecraft:potted_warped_fungus");
    InitFlowerPot("minecraft:potted_crimson_roots");
    InitFlowerPot("minecraft:potted_warped_roots");
    InitSimpleFullBlock("minecraft:lodestone");
    InitSimpleFullBlock("minecraft:blackstone");

    init_stair_props("minecraft:blackstone_stairs");

    init_wall_props("minecraft:blackstone_wall");

    init_slab("minecraft:blackstone_slab");

    InitSimpleFullBlock("minecraft:polished_blackstone");
    InitSimpleFullBlock("minecraft:polished_blackstone_bricks");
    InitSimpleFullBlock("minecraft:cracked_polished_blackstone_bricks");
    InitSimpleFullBlock("minecraft:chiseled_polished_blackstone");

    init_slab("minecraft:polished_blackstone_brick_slab");

    init_stair_props("minecraft:polished_blackstone_brick_stairs");

    init_wall_props("minecraft:polished_blackstone_brick_wall");

    InitSimpleFullBlock("minecraft:gilded_blackstone");

    init_stair_props("minecraft:polished_blackstone_stairs");

    init_slab("minecraft:polished_blackstone_slab");

    init_pressure_plate("minecraft:polished_blackstone_pressure_plate");

    init_button("minecraft:polished_blackstone_button");

    init_wall_props("minecraft:polished_blackstone_wall");

    InitSimpleFullBlock("minecraft:chiseled_nether_bricks");
    InitSimpleFullBlock("minecraft:cracked_nether_bricks");
    InitSimpleFullBlock("minecraft:quartz_bricks");

    init_candle("minecraft:candle");
    init_candle("minecraft:white_candle");
    init_candle("minecraft:orange_candle");
    init_candle("minecraft:magenta_candle");
    init_candle("minecraft:light_blue_candle");
    init_candle("minecraft:yellow_candle");
    init_candle("minecraft:lime_candle");
    init_candle("minecraft:pink_candle");
    init_candle("minecraft:gray_candle");
    init_candle("minecraft:light_gray_candle");
    init_candle("minecraft:cyan_candle");
    init_candle("minecraft:purple_candle");
    init_candle("minecraft:blue_candle");
    init_candle("minecraft:brown_candle");
    init_candle("minecraft:green_candle");
    init_candle("minecraft:red_candle");
    init_candle("minecraft:black_candle");

    init_candle_cake("minecraft:candle_cake");
    init_candle_cake("minecraft:white_candle_cake");
    init_candle_cake("minecraft:orange_candle_cake");
    init_candle_cake("minecraft:magenta_candle_cake");
    init_candle_cake("minecraft:light_blue_candle_cake");
    init_candle_cake("minecraft:yellow_candle_cake");
    init_candle_cake("minecraft:lime_candle_cake");
    init_candle_cake("minecraft:pink_candle_cake");
    init_candle_cake("minecraft:gray_candle_cake");
    init_candle_cake("minecraft:light_gray_candle_cake");
    init_candle_cake("minecraft:cyan_candle_cake");
    init_candle_cake("minecraft:purple_candle_cake");
    init_candle_cake("minecraft:blue_candle_cake");
    init_candle_cake("minecraft:brown_candle_cake");
    init_candle_cake("minecraft:green_candle_cake");
    init_candle_cake("minecraft:red_candle_cake");
    init_candle_cake("minecraft:black_candle_cake");

    InitSimpleFullBlock("minecraft:amethyst_block");
    InitSimpleFullBlock("minecraft:budding_amethyst");

    init_amethyst_cluster("minecraft:amethyst_cluster");
    init_amethyst_cluster("minecraft:large_amethyst_bud");
    init_amethyst_cluster("minecraft:medium_amethyst_bud");
    init_amethyst_cluster("minecraft:small_amethyst_bud");

    InitSimpleFullBlock("minecraft:tuff");

    InitSimpleFullBlock("minecraft:calcite");

    InitSimpleFullBlock("minecraft:tinted_glass");

    // @TODO(traks) correct collision model, support model is correct though!
    InitSimpleBlockWithModels("minecraft:powder_snow", BLOCK_MODEL_EMPTY, BLOCK_MODEL_EMPTY, BLOCK_MODEL_EMPTY, 1);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:sculk_sensor");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_SCULK_SENSOR_PHASE, "inactive");
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    InitSimpleFullBlock("minecraft:sculk");
    InitMultiFaceBlock("minecraft:sculk_vein");

    block_type = register_block_type("minecraft:sculk_catalyst");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_BLOOM, "false");
    finalise_block_props(props);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    block_type = register_block_type("minecraft:sculk_shrieker");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_SHRIEKING, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_CAN_SUMMON, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_Y_8);
    SetLightReductionForAllStates(props, 0);

    InitSimpleFullBlock("minecraft:oxidized_copper");
    InitSimpleFullBlock("minecraft:weathered_copper");
    InitSimpleFullBlock("minecraft:exposed_copper");
    InitSimpleFullBlock("minecraft:copper_block");
    InitSimpleFullBlock("minecraft:copper_ore");
    InitSimpleFullBlock("minecraft:deepslate_copper_ore");
    InitSimpleFullBlock("minecraft:oxidized_cut_copper");
    InitSimpleFullBlock("minecraft:weathered_cut_copper");
    InitSimpleFullBlock("minecraft:exposed_cut_copper");
    InitSimpleFullBlock("minecraft:cut_copper");

    init_stair_props("minecraft:oxidized_cut_copper_stairs");
    init_stair_props("minecraft:weathered_cut_copper_stairs");
    init_stair_props("minecraft:exposed_cut_copper_stairs");
    init_stair_props("minecraft:cut_copper_stairs");

    init_slab("minecraft:oxidized_cut_copper_slab");
    init_slab("minecraft:weathered_cut_copper_slab");
    init_slab("minecraft:exposed_cut_copper_slab");
    init_slab("minecraft:cut_copper_slab");

    InitSimpleFullBlock("minecraft:waxed_copper_block");
    InitSimpleFullBlock("minecraft:waxed_weathered_copper");
    InitSimpleFullBlock("minecraft:waxed_exposed_copper");
    InitSimpleFullBlock("minecraft:waxed_oxidized_copper");
    InitSimpleFullBlock("minecraft:waxed_oxidized_cut_copper");
    InitSimpleFullBlock("minecraft:waxed_weathered_cut_copper");
    InitSimpleFullBlock("minecraft:waxed_exposed_cut_copper");
    InitSimpleFullBlock("minecraft:waxed_cut_copper");

    init_stair_props("minecraft:waxed_oxidized_cut_copper_stairs");
    init_stair_props("minecraft:waxed_weathered_cut_copper_stairs");
    init_stair_props("minecraft:waxed_exposed_cut_copper_stairs");
    init_stair_props("minecraft:waxed_cut_copper_stairs");

    init_slab("minecraft:waxed_oxidized_cut_copper_slab");
    init_slab("minecraft:waxed_weathered_cut_copper_slab");
    init_slab("minecraft:waxed_exposed_cut_copper_slab");
    init_slab("minecraft:waxed_cut_copper_slab");

    // TODO(traks) block models + light reduction
    block_type = register_block_type("minecraft:lightning_rod");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "up");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:pointed_dripstone");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_VERTICAL_DIRECTION, "up");
    add_block_property(props, BLOCK_PROPERTY_DRIPSTONE_THICKNESS, "tip");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    InitSimpleFullBlock("minecraft:dripstone_block");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:cave_vines");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_25, "0");
    add_block_property(props, BLOCK_PROPERTY_BERRIES, "false");
    finalise_block_props(props);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:cave_vines_plant");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_BERRIES, "false");
    finalise_block_props(props);

    InitSimpleEmptyBlock("minecraft:spore_blossom");

    // TODO(traks): block models
    InitSimpleBlockWithModels("minecraft:azalea", BLOCK_MODEL_EMPTY, BLOCK_MODEL_EMPTY, BLOCK_MODEL_EMPTY, 0);
    InitSimpleBlockWithModels("minecraft:flowering_azalea", BLOCK_MODEL_EMPTY, BLOCK_MODEL_EMPTY, BLOCK_MODEL_EMPTY, 0);

    InitCarpet("minecraft:moss_carpet");
    InitSimpleFullBlock("minecraft:moss_block");

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:big_dripleaf");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_DRIPLEAF_TILT, "none");
    finalise_block_props(props);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:big_dripleaf_stem");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);

    // TODO(traks): block models + light reduction
    block_type = register_block_type("minecraft:small_dripleaf");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "lower");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:hanging_roots");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitSimpleFullBlock("minecraft:rooted_dirt");
    InitSimpleFullBlock("minecraft:mud");

    init_pillar("minecraft:deepslate");

    InitSimpleFullBlock("minecraft:cobbled_deepslate");
    init_stair_props("minecraft:cobbled_deepslate_stairs");
    init_slab("minecraft:cobbled_deepslate_slab");
    init_wall_props("minecraft:cobbled_deepslate_wall");

    InitSimpleFullBlock("minecraft:polished_deepslate");
    init_stair_props("minecraft:polished_deepslate_stairs");
    init_slab("minecraft:polished_deepslate_slab");
    init_wall_props("minecraft:polished_deepslate_wall");

    InitSimpleFullBlock("minecraft:deepslate_tiles");
    init_stair_props("minecraft:deepslate_tile_stairs");
    init_slab("minecraft:deepslate_tile_slab");
    init_wall_props("minecraft:deepslate_tile_wall");

    InitSimpleFullBlock("minecraft:deepslate_bricks");
    init_stair_props("minecraft:deepslate_brick_stairs");
    init_slab("minecraft:deepslate_brick_slab");
    init_wall_props("minecraft:deepslate_brick_wall");

    InitSimpleFullBlock("minecraft:chiseled_deepslate");
    InitSimpleFullBlock("minecraft:cracked_deepslate_bricks");
    InitSimpleFullBlock("minecraft:cracked_deepslate_tiles");

    init_pillar("minecraft:infested_deepslate");

    InitSimpleFullBlock("minecraft:smooth_basalt");
    InitSimpleFullBlock("minecraft:raw_iron_block");
    InitSimpleFullBlock("minecraft:raw_copper_block");
    InitSimpleFullBlock("minecraft:raw_gold_block");

    InitFlowerPot("minecraft:potted_azalea_bush");
    InitFlowerPot("minecraft:potted_flowering_azalea_bush");

    init_pillar("minecraft:ochre_froglight");
    init_pillar("minecraft:verdant_froglight");
    init_pillar("minecraft:pearlescent_froglight");
    InitSimpleEmptyBlock("minecraft:frogspawn");
    InitSimpleFullBlock("minecraft:reinforced_deepslate");

    serv->vanilla_block_state_count = serv->actual_block_state_count;

    InitSimpleFullBlock("blaze:unknown");

    assert(MAX_BLOCK_STATES >= serv->actual_block_state_count);
}
