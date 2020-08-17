#ifndef CODEC_H
#define CODEC_H

#include <stddef.h>
#include <stdint.h>
#include <assert.h>

#define ARRAY_SIZE(x) (sizeof (x) / sizeof *(x))

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define ABS(a) ((a) < 0 ? -(a) : (a))

#define CLAMP(x, l, u) (MAX(MIN((x), (u)), (l)))

#define PI (3.141592653589f)

#define DEGREES_PER_RADIAN (360 / (2 * PI))

typedef int8_t mc_byte;
typedef int16_t mc_short;
typedef int32_t mc_int;
typedef int64_t mc_long;
typedef uint8_t mc_ubyte;
typedef uint16_t mc_ushort;
typedef uint32_t mc_uint;
typedef uint64_t mc_ulong;

typedef struct {
    unsigned char * ptr;
    mc_int size;
    mc_int index;
} memory_arena;

typedef struct {
    mc_int x;
    mc_int y;
    mc_int z;
} net_block_pos;

typedef struct {
    // @TODO(traks) could the compiler think that the buffer points to some
    // buffer that contains this struct itself, meaning it has to reload fields
    // after we write something to it?
    unsigned char * buf;
    int limit;
    int index;
    int error;
} buffer_cursor;

#define NET_STRING(x) ((net_string) {.size = sizeof (x) - 1, .ptr = (x)})

typedef struct {
    mc_int size;
    void * ptr;
} net_string;

enum nbt_tag {
    NBT_TAG_END,
    NBT_TAG_BYTE,
    NBT_TAG_SHORT,
    NBT_TAG_INT,
    NBT_TAG_LONG,
    NBT_TAG_FLOAT,
    NBT_TAG_DOUBLE,
    NBT_TAG_BYTE_ARRAY,
    NBT_TAG_STRING,
    NBT_TAG_LIST,
    NBT_TAG_COMPOUND,
    NBT_TAG_INT_ARRAY,
    NBT_TAG_LONG_ARRAY,

    // our own tags for internal use
    NBT_TAG_LIST_END,
    NBT_TAG_COMPOUND_IN_LIST,
    NBT_TAG_LIST_IN_LIST,
};

typedef union {
    struct {
        mc_uint buffer_index:22;
        mc_uint tag:5;
        mc_uint element_tag:5;
    };
    mc_uint next_compound_entry_offset;
    mc_uint list_size;
} nbt_tape_entry;

#define MAX_CHUNK_CACHE_RADIUS (10)

#define MAX_CHUNK_CACHE_DIAM (2 * MAX_CHUNK_CACHE_RADIUS + 1)

#define KEEP_ALIVE_SPACING (10 * 20)

#define KEEP_ALIVE_TIMEOUT (30 * 20)

#define MAX_CHUNK_SENDS_PER_TICK (2)

#define MAX_CHUNK_LOADS_PER_TICK (2)

// must be power of 2
#define MAX_ENTITIES (1024)

#define MAX_PLAYERS (100)

// in network id order
enum gamemode {
    GAMEMODE_SURVIVAL,
    GAMEMODE_CREATIVE,
    GAMEMODE_ADVENTURE,
    GAMEMODE_SPECTATOR,
};

// @NOTE(traks) I think of the Minecraft coordinate system as follows:
//
//        +Y
//        |
//        |
//        *---- +X (270 degrees)
//       /
//      /
//     +Z (0 degrees)
//
// Then north naturally corresponds to -Z, east to +X, etc. However, entity
// rotations along the Y axis are the opposite to what you might expect: adding
// degrees rotates clockwise instead of counter-clockwise (as is common in
// mathematics).

// in network id order
enum direction {
    DIRECTION_NEG_Y, // down
    DIRECTION_POS_Y, // up
    DIRECTION_NEG_Z, // north
    DIRECTION_POS_Z, // south
    DIRECTION_NEG_X, // west
    DIRECTION_POS_X, // east
};

typedef struct {
    mc_short x;
    mc_short z;
} chunk_pos;

#define CHUNK_LOADED (1u << 0)

typedef struct {
    int index_in_bucket;
    mc_ushort block_states[4096];
} chunk_section;

#define CHUNK_SECTIONS_PER_BUCKET (64)

typedef struct chunk_section_bucket chunk_section_bucket;

struct chunk_section_bucket {
    chunk_section chunk_sections[CHUNK_SECTIONS_PER_BUCKET];
    // @TODO(traks) we use 2 * CHUNK_SECTIONS_PER_BUCKET 4096-byte pages for the
    // block states in the chunk sections. How much of the next page do we use?
    chunk_section_bucket * next;
    chunk_section_bucket * prev;
    int used_sections;
    // @TODO(traks) store this in longs?
    unsigned char used_map[CHUNK_SECTIONS_PER_BUCKET];
};

typedef struct {
    mc_ushort x:4;
    mc_ushort y:8;
    mc_ushort z:4;
} compact_chunk_block_pos;

typedef struct {
    chunk_section * sections[16];
    mc_ushort non_air_count[16];
    // need shorts to store 257 different heights
    mc_ushort motion_blocking_height_map[256];

    // increment if you want to keep a chunk available in the map, decrement
    // if you no longer care for the chunk.
    // If = 0 the chunk will be removed from the map at some point.
    mc_uint available_interest;
    unsigned flags;

    // @TODO(traks) more changed blocks, better compression. Figure out when
    // this limit can be exceeded. I highly doubt more than 16 blocks will be
    // changed per chunk per tick due to players except if player density is
    // very high.
    compact_chunk_block_pos changed_blocks[16];
    mc_ubyte changed_block_count;
} chunk;

#define CHUNKS_PER_BUCKET (32)

#define CHUNK_MAP_SIZE (1024)

typedef struct chunk_bucket chunk_bucket;

struct chunk_bucket {
    chunk_bucket * next_bucket;
    int size;
    chunk_pos positions[CHUNKS_PER_BUCKET];
    chunk chunks[CHUNKS_PER_BUCKET];
};

typedef struct {
    unsigned char value_count;
    // name size, name, value size, value, value size, value, etc.
    unsigned char tape[255];
} block_property_spec;

typedef struct {
    mc_ushort base_state;
    unsigned char property_count;
    unsigned char property_specs[8];
    unsigned char default_value_indices[8];
} block_properties;

enum item_type {
    ITEM_AIR,
    ITEM_STONE,
    ITEM_GRANITE,
    ITEM_POLISHED_GRANITE,
    ITEM_DIORITE,
    ITEM_POLISHED_DIORITE,
    ITEM_ANDESITE,
    ITEM_POLISHED_ANDESITE,
    ITEM_GRASS_BLOCK,
    ITEM_DIRT,
    ITEM_COARSE_DIRT,
    ITEM_PODZOL,
    ITEM_CRIMSON_NYLIUM,
    ITEM_WARPED_NYLIUM,
    ITEM_COBBLESTONE,
    ITEM_OAK_PLANKS,
    ITEM_SPRUCE_PLANKS,
    ITEM_BIRCH_PLANKS,
    ITEM_JUNGLE_PLANKS,
    ITEM_ACACIA_PLANKS,
    ITEM_DARK_OAK_PLANKS,
    ITEM_CRIMSON_PLANKS,
    ITEM_WARPED_PLANKS,
    ITEM_OAK_SAPLING,
    ITEM_SPRUCE_SAPLING,
    ITEM_BIRCH_SAPLING,
    ITEM_JUNGLE_SAPLING,
    ITEM_ACACIA_SAPLING,
    ITEM_DARK_OAK_SAPLING,
    ITEM_BEDROCK,
    ITEM_SAND,
    ITEM_RED_SAND,
    ITEM_GRAVEL,
    ITEM_GOLD_ORE,
    ITEM_IRON_ORE,
    ITEM_COAL_ORE,
    ITEM_NETHER_GOLD_ORE,
    ITEM_OAK_LOG,
    ITEM_SPRUCE_LOG,
    ITEM_BIRCH_LOG,
    ITEM_JUNGLE_LOG,
    ITEM_ACACIA_LOG,
    ITEM_DARK_OAK_LOG,
    ITEM_CRIMSON_STEM,
    ITEM_WARPED_STEM,
    ITEM_STRIPPED_OAK_LOG,
    ITEM_STRIPPED_SPRUCE_LOG,
    ITEM_STRIPPED_BIRCH_LOG,
    ITEM_STRIPPED_JUNGLE_LOG,
    ITEM_STRIPPED_ACACIA_LOG,
    ITEM_STRIPPED_DARK_OAK_LOG,
    ITEM_STRIPPED_CRIMSON_STEM,
    ITEM_STRIPPED_WARPED_STEM,
    ITEM_STRIPPED_OAK_WOOD,
    ITEM_STRIPPED_SPRUCE_WOOD,
    ITEM_STRIPPED_BIRCH_WOOD,
    ITEM_STRIPPED_JUNGLE_WOOD,
    ITEM_STRIPPED_ACACIA_WOOD,
    ITEM_STRIPPED_DARK_OAK_WOOD,
    ITEM_STRIPPED_CRIMSON_HYPHAE,
    ITEM_STRIPPED_WARPED_HYPHAE,
    ITEM_OAK_WOOD,
    ITEM_SPRUCE_WOOD,
    ITEM_BIRCH_WOOD,
    ITEM_JUNGLE_WOOD,
    ITEM_ACACIA_WOOD,
    ITEM_DARK_OAK_WOOD,
    ITEM_CRIMSON_HYPHAE,
    ITEM_WARPED_HYPHAE,
    ITEM_OAK_LEAVES,
    ITEM_SPRUCE_LEAVES,
    ITEM_BIRCH_LEAVES,
    ITEM_JUNGLE_LEAVES,
    ITEM_ACACIA_LEAVES,
    ITEM_DARK_OAK_LEAVES,
    ITEM_SPONGE,
    ITEM_WET_SPONGE,
    ITEM_GLASS,
    ITEM_LAPIS_ORE,
    ITEM_LAPIS_BLOCK,
    ITEM_DISPENSER,
    ITEM_SANDSTONE,
    ITEM_CHISELED_SANDSTONE,
    ITEM_CUT_SANDSTONE,
    ITEM_NOTE_BLOCK,
    ITEM_POWERED_RAIL,
    ITEM_DETECTOR_RAIL,
    ITEM_STICKY_PISTON,
    ITEM_COBWEB,
    ITEM_GRASS,
    ITEM_FERN,
    ITEM_DEAD_BUSH,
    ITEM_SEAGRASS,
    ITEM_SEA_PICKLE,
    ITEM_PISTON,
    ITEM_WHITE_WOOL,
    ITEM_ORANGE_WOOL,
    ITEM_MAGENTA_WOOL,
    ITEM_LIGHT_BLUE_WOOL,
    ITEM_YELLOW_WOOL,
    ITEM_LIME_WOOL,
    ITEM_PINK_WOOL,
    ITEM_GRAY_WOOL,
    ITEM_LIGHT_GRAY_WOOL,
    ITEM_CYAN_WOOL,
    ITEM_PURPLE_WOOL,
    ITEM_BLUE_WOOL,
    ITEM_BROWN_WOOL,
    ITEM_GREEN_WOOL,
    ITEM_RED_WOOL,
    ITEM_BLACK_WOOL,
    ITEM_DANDELION,
    ITEM_POPPY,
    ITEM_BLUE_ORCHID,
    ITEM_ALLIUM,
    ITEM_AZURE_BLUET,
    ITEM_RED_TULIP,
    ITEM_ORANGE_TULIP,
    ITEM_WHITE_TULIP,
    ITEM_PINK_TULIP,
    ITEM_OXEYE_DAISY,
    ITEM_CORNFLOWER,
    ITEM_LILY_OF_THE_VALLEY,
    ITEM_WITHER_ROSE,
    ITEM_BROWN_MUSHROOM,
    ITEM_RED_MUSHROOM,
    ITEM_CRIMSON_FUNGUS,
    ITEM_WARPED_FUNGUS,
    ITEM_CRIMSON_ROOTS,
    ITEM_WARPED_ROOTS,
    ITEM_NETHER_SPROUTS,
    ITEM_WEEPING_VINES,
    ITEM_TWISTING_VINES,
    ITEM_SUGAR_CANE,
    ITEM_KELP,
    ITEM_BAMBOO,
    ITEM_GOLD_BLOCK,
    ITEM_IRON_BLOCK,
    ITEM_OAK_SLAB,
    ITEM_SPRUCE_SLAB,
    ITEM_BIRCH_SLAB,
    ITEM_JUNGLE_SLAB,
    ITEM_ACACIA_SLAB,
    ITEM_DARK_OAK_SLAB,
    ITEM_CRIMSON_SLAB,
    ITEM_WARPED_SLAB,
    ITEM_STONE_SLAB,
    ITEM_SMOOTH_STONE_SLAB,
    ITEM_SANDSTONE_SLAB,
    ITEM_CUT_SANDSTONE_SLAB,
    ITEM_PETRIFIED_OAK_SLAB,
    ITEM_COBBLESTONE_SLAB,
    ITEM_BRICK_SLAB,
    ITEM_STONE_BRICK_SLAB,
    ITEM_NETHER_BRICK_SLAB,
    ITEM_QUARTZ_SLAB,
    ITEM_RED_SANDSTONE_SLAB,
    ITEM_CUT_RED_SANDSTONE_SLAB,
    ITEM_PURPUR_SLAB,
    ITEM_PRISMARINE_SLAB,
    ITEM_PRISMARINE_BRICK_SLAB,
    ITEM_DARK_PRISMARINE_SLAB,
    ITEM_SMOOTH_QUARTZ,
    ITEM_SMOOTH_RED_SANDSTONE,
    ITEM_SMOOTH_SANDSTONE,
    ITEM_SMOOTH_STONE,
    ITEM_BRICKS,
    ITEM_TNT,
    ITEM_BOOKSHELF,
    ITEM_MOSSY_COBBLESTONE,
    ITEM_OBSIDIAN,
    ITEM_TORCH,
    ITEM_END_ROD,
    ITEM_CHORUS_PLANT,
    ITEM_CHORUS_FLOWER,
    ITEM_PURPUR_BLOCK,
    ITEM_PURPUR_PILLAR,
    ITEM_PURPUR_STAIRS,
    ITEM_SPAWNER,
    ITEM_OAK_STAIRS,
    ITEM_CHEST,
    ITEM_DIAMOND_ORE,
    ITEM_DIAMOND_BLOCK,
    ITEM_CRAFTING_TABLE,
    ITEM_FARMLAND,
    ITEM_FURNACE,
    ITEM_LADDER,
    ITEM_RAIL,
    ITEM_COBBLESTONE_STAIRS,
    ITEM_LEVER,
    ITEM_STONE_PRESSURE_PLATE,
    ITEM_OAK_PRESSURE_PLATE,
    ITEM_SPRUCE_PRESSURE_PLATE,
    ITEM_BIRCH_PRESSURE_PLATE,
    ITEM_JUNGLE_PRESSURE_PLATE,
    ITEM_ACACIA_PRESSURE_PLATE,
    ITEM_DARK_OAK_PRESSURE_PLATE,
    ITEM_CRIMSON_PRESSURE_PLATE,
    ITEM_WARPED_PRESSURE_PLATE,
    ITEM_POLISHED_BLACKSTONE_PRESSURE_PLATE,
    ITEM_REDSTONE_ORE,
    ITEM_REDSTONE_TORCH,
    ITEM_SNOW,
    ITEM_ICE,
    ITEM_SNOW_BLOCK,
    ITEM_CACTUS,
    ITEM_CLAY,
    ITEM_JUKEBOX,
    ITEM_OAK_FENCE,
    ITEM_SPRUCE_FENCE,
    ITEM_BIRCH_FENCE,
    ITEM_JUNGLE_FENCE,
    ITEM_ACACIA_FENCE,
    ITEM_DARK_OAK_FENCE,
    ITEM_CRIMSON_FENCE,
    ITEM_WARPED_FENCE,
    ITEM_PUMPKIN,
    ITEM_CARVED_PUMPKIN,
    ITEM_NETHERRACK,
    ITEM_SOUL_SAND,
    ITEM_SOUL_SOIL,
    ITEM_BASALT,
    ITEM_POLISHED_BASALT,
    ITEM_SOUL_TORCH,
    ITEM_GLOWSTONE,
    ITEM_JACK_O_LANTERN,
    ITEM_OAK_TRAPDOOR,
    ITEM_SPRUCE_TRAPDOOR,
    ITEM_BIRCH_TRAPDOOR,
    ITEM_JUNGLE_TRAPDOOR,
    ITEM_ACACIA_TRAPDOOR,
    ITEM_DARK_OAK_TRAPDOOR,
    ITEM_CRIMSON_TRAPDOOR,
    ITEM_WARPED_TRAPDOOR,
    ITEM_INFESTED_STONE,
    ITEM_INFESTED_COBBLESTONE,
    ITEM_INFESTED_STONE_BRICKS,
    ITEM_INFESTED_MOSSY_STONE_BRICKS,
    ITEM_INFESTED_CRACKED_STONE_BRICKS,
    ITEM_INFESTED_CHISELED_STONE_BRICKS,
    ITEM_STONE_BRICKS,
    ITEM_MOSSY_STONE_BRICKS,
    ITEM_CRACKED_STONE_BRICKS,
    ITEM_CHISELED_STONE_BRICKS,
    ITEM_BROWN_MUSHROOM_BLOCK,
    ITEM_RED_MUSHROOM_BLOCK,
    ITEM_MUSHROOM_STEM,
    ITEM_IRON_BARS,
    ITEM_CHAIN,
    ITEM_GLASS_PANE,
    ITEM_MELON,
    ITEM_VINE,
    ITEM_OAK_FENCE_GATE,
    ITEM_SPRUCE_FENCE_GATE,
    ITEM_BIRCH_FENCE_GATE,
    ITEM_JUNGLE_FENCE_GATE,
    ITEM_ACACIA_FENCE_GATE,
    ITEM_DARK_OAK_FENCE_GATE,
    ITEM_CRIMSON_FENCE_GATE,
    ITEM_WARPED_FENCE_GATE,
    ITEM_BRICK_STAIRS,
    ITEM_STONE_BRICK_STAIRS,
    ITEM_MYCELIUM,
    ITEM_LILY_PAD,
    ITEM_NETHER_BRICKS,
    ITEM_CRACKED_NETHER_BRICKS,
    ITEM_CHISELED_NETHER_BRICKS,
    ITEM_NETHER_BRICK_FENCE,
    ITEM_NETHER_BRICK_STAIRS,
    ITEM_ENCHANTING_TABLE,
    ITEM_END_PORTAL_FRAME,
    ITEM_END_STONE,
    ITEM_END_STONE_BRICKS,
    ITEM_DRAGON_EGG,
    ITEM_REDSTONE_LAMP,
    ITEM_SANDSTONE_STAIRS,
    ITEM_EMERALD_ORE,
    ITEM_ENDER_CHEST,
    ITEM_TRIPWIRE_HOOK,
    ITEM_EMERALD_BLOCK,
    ITEM_SPRUCE_STAIRS,
    ITEM_BIRCH_STAIRS,
    ITEM_JUNGLE_STAIRS,
    ITEM_CRIMSON_STAIRS,
    ITEM_WARPED_STAIRS,
    ITEM_COMMAND_BLOCK,
    ITEM_BEACON,
    ITEM_COBBLESTONE_WALL,
    ITEM_MOSSY_COBBLESTONE_WALL,
    ITEM_BRICK_WALL,
    ITEM_PRISMARINE_WALL,
    ITEM_RED_SANDSTONE_WALL,
    ITEM_MOSSY_STONE_BRICK_WALL,
    ITEM_GRANITE_WALL,
    ITEM_STONE_BRICK_WALL,
    ITEM_NETHER_BRICK_WALL,
    ITEM_ANDESITE_WALL,
    ITEM_RED_NETHER_BRICK_WALL,
    ITEM_SANDSTONE_WALL,
    ITEM_END_STONE_BRICK_WALL,
    ITEM_DIORITE_WALL,
    ITEM_BLACKSTONE_WALL,
    ITEM_POLISHED_BLACKSTONE_WALL,
    ITEM_POLISHED_BLACKSTONE_BRICK_WALL,
    ITEM_STONE_BUTTON,
    ITEM_OAK_BUTTON,
    ITEM_SPRUCE_BUTTON,
    ITEM_BIRCH_BUTTON,
    ITEM_JUNGLE_BUTTON,
    ITEM_ACACIA_BUTTON,
    ITEM_DARK_OAK_BUTTON,
    ITEM_CRIMSON_BUTTON,
    ITEM_WARPED_BUTTON,
    ITEM_POLISHED_BLACKSTONE_BUTTON,
    ITEM_ANVIL,
    ITEM_CHIPPED_ANVIL,
    ITEM_DAMAGED_ANVIL,
    ITEM_TRAPPED_CHEST,
    ITEM_LIGHT_WEIGHTED_PRESSURE_PLATE,
    ITEM_HEAVY_WEIGHTED_PRESSURE_PLATE,
    ITEM_DAYLIGHT_DETECTOR,
    ITEM_REDSTONE_BLOCK,
    ITEM_NETHER_QUARTZ_ORE,
    ITEM_HOPPER,
    ITEM_CHISELED_QUARTZ_BLOCK,
    ITEM_QUARTZ_BLOCK,
    ITEM_QUARTZ_BRICKS,
    ITEM_QUARTZ_PILLAR,
    ITEM_QUARTZ_STAIRS,
    ITEM_ACTIVATOR_RAIL,
    ITEM_DROPPER,
    ITEM_WHITE_TERRACOTTA,
    ITEM_ORANGE_TERRACOTTA,
    ITEM_MAGENTA_TERRACOTTA,
    ITEM_LIGHT_BLUE_TERRACOTTA,
    ITEM_YELLOW_TERRACOTTA,
    ITEM_LIME_TERRACOTTA,
    ITEM_PINK_TERRACOTTA,
    ITEM_GRAY_TERRACOTTA,
    ITEM_LIGHT_GRAY_TERRACOTTA,
    ITEM_CYAN_TERRACOTTA,
    ITEM_PURPLE_TERRACOTTA,
    ITEM_BLUE_TERRACOTTA,
    ITEM_BROWN_TERRACOTTA,
    ITEM_GREEN_TERRACOTTA,
    ITEM_RED_TERRACOTTA,
    ITEM_BLACK_TERRACOTTA,
    ITEM_BARRIER,
    ITEM_IRON_TRAPDOOR,
    ITEM_HAY_BLOCK,
    ITEM_WHITE_CARPET,
    ITEM_ORANGE_CARPET,
    ITEM_MAGENTA_CARPET,
    ITEM_LIGHT_BLUE_CARPET,
    ITEM_YELLOW_CARPET,
    ITEM_LIME_CARPET,
    ITEM_PINK_CARPET,
    ITEM_GRAY_CARPET,
    ITEM_LIGHT_GRAY_CARPET,
    ITEM_CYAN_CARPET,
    ITEM_PURPLE_CARPET,
    ITEM_BLUE_CARPET,
    ITEM_BROWN_CARPET,
    ITEM_GREEN_CARPET,
    ITEM_RED_CARPET,
    ITEM_BLACK_CARPET,
    ITEM_TERRACOTTA,
    ITEM_COAL_BLOCK,
    ITEM_PACKED_ICE,
    ITEM_ACACIA_STAIRS,
    ITEM_DARK_OAK_STAIRS,
    ITEM_SLIME_BLOCK,
    ITEM_GRASS_PATH,
    ITEM_SUNFLOWER,
    ITEM_LILAC,
    ITEM_ROSE_BUSH,
    ITEM_PEONY,
    ITEM_TALL_GRASS,
    ITEM_LARGE_FERN,
    ITEM_WHITE_STAINED_GLASS,
    ITEM_ORANGE_STAINED_GLASS,
    ITEM_MAGENTA_STAINED_GLASS,
    ITEM_LIGHT_BLUE_STAINED_GLASS,
    ITEM_YELLOW_STAINED_GLASS,
    ITEM_LIME_STAINED_GLASS,
    ITEM_PINK_STAINED_GLASS,
    ITEM_GRAY_STAINED_GLASS,
    ITEM_LIGHT_GRAY_STAINED_GLASS,
    ITEM_CYAN_STAINED_GLASS,
    ITEM_PURPLE_STAINED_GLASS,
    ITEM_BLUE_STAINED_GLASS,
    ITEM_BROWN_STAINED_GLASS,
    ITEM_GREEN_STAINED_GLASS,
    ITEM_RED_STAINED_GLASS,
    ITEM_BLACK_STAINED_GLASS,
    ITEM_WHITE_STAINED_GLASS_PANE,
    ITEM_ORANGE_STAINED_GLASS_PANE,
    ITEM_MAGENTA_STAINED_GLASS_PANE,
    ITEM_LIGHT_BLUE_STAINED_GLASS_PANE,
    ITEM_YELLOW_STAINED_GLASS_PANE,
    ITEM_LIME_STAINED_GLASS_PANE,
    ITEM_PINK_STAINED_GLASS_PANE,
    ITEM_GRAY_STAINED_GLASS_PANE,
    ITEM_LIGHT_GRAY_STAINED_GLASS_PANE,
    ITEM_CYAN_STAINED_GLASS_PANE,
    ITEM_PURPLE_STAINED_GLASS_PANE,
    ITEM_BLUE_STAINED_GLASS_PANE,
    ITEM_BROWN_STAINED_GLASS_PANE,
    ITEM_GREEN_STAINED_GLASS_PANE,
    ITEM_RED_STAINED_GLASS_PANE,
    ITEM_BLACK_STAINED_GLASS_PANE,
    ITEM_PRISMARINE,
    ITEM_PRISMARINE_BRICKS,
    ITEM_DARK_PRISMARINE,
    ITEM_PRISMARINE_STAIRS,
    ITEM_PRISMARINE_BRICK_STAIRS,
    ITEM_DARK_PRISMARINE_STAIRS,
    ITEM_SEA_LANTERN,
    ITEM_RED_SANDSTONE,
    ITEM_CHISELED_RED_SANDSTONE,
    ITEM_CUT_RED_SANDSTONE,
    ITEM_RED_SANDSTONE_STAIRS,
    ITEM_REPEATING_COMMAND_BLOCK,
    ITEM_CHAIN_COMMAND_BLOCK,
    ITEM_MAGMA_BLOCK,
    ITEM_NETHER_WART_BLOCK,
    ITEM_WARPED_WART_BLOCK,
    ITEM_RED_NETHER_BRICKS,
    ITEM_BONE_BLOCK,
    ITEM_STRUCTURE_VOID,
    ITEM_OBSERVER,
    ITEM_SHULKER_BOX,
    ITEM_WHITE_SHULKER_BOX,
    ITEM_ORANGE_SHULKER_BOX,
    ITEM_MAGENTA_SHULKER_BOX,
    ITEM_LIGHT_BLUE_SHULKER_BOX,
    ITEM_YELLOW_SHULKER_BOX,
    ITEM_LIME_SHULKER_BOX,
    ITEM_PINK_SHULKER_BOX,
    ITEM_GRAY_SHULKER_BOX,
    ITEM_LIGHT_GRAY_SHULKER_BOX,
    ITEM_CYAN_SHULKER_BOX,
    ITEM_PURPLE_SHULKER_BOX,
    ITEM_BLUE_SHULKER_BOX,
    ITEM_BROWN_SHULKER_BOX,
    ITEM_GREEN_SHULKER_BOX,
    ITEM_RED_SHULKER_BOX,
    ITEM_BLACK_SHULKER_BOX,
    ITEM_WHITE_GLAZED_TERRACOTTA,
    ITEM_ORANGE_GLAZED_TERRACOTTA,
    ITEM_MAGENTA_GLAZED_TERRACOTTA,
    ITEM_LIGHT_BLUE_GLAZED_TERRACOTTA,
    ITEM_YELLOW_GLAZED_TERRACOTTA,
    ITEM_LIME_GLAZED_TERRACOTTA,
    ITEM_PINK_GLAZED_TERRACOTTA,
    ITEM_GRAY_GLAZED_TERRACOTTA,
    ITEM_LIGHT_GRAY_GLAZED_TERRACOTTA,
    ITEM_CYAN_GLAZED_TERRACOTTA,
    ITEM_PURPLE_GLAZED_TERRACOTTA,
    ITEM_BLUE_GLAZED_TERRACOTTA,
    ITEM_BROWN_GLAZED_TERRACOTTA,
    ITEM_GREEN_GLAZED_TERRACOTTA,
    ITEM_RED_GLAZED_TERRACOTTA,
    ITEM_BLACK_GLAZED_TERRACOTTA,
    ITEM_WHITE_CONCRETE,
    ITEM_ORANGE_CONCRETE,
    ITEM_MAGENTA_CONCRETE,
    ITEM_LIGHT_BLUE_CONCRETE,
    ITEM_YELLOW_CONCRETE,
    ITEM_LIME_CONCRETE,
    ITEM_PINK_CONCRETE,
    ITEM_GRAY_CONCRETE,
    ITEM_LIGHT_GRAY_CONCRETE,
    ITEM_CYAN_CONCRETE,
    ITEM_PURPLE_CONCRETE,
    ITEM_BLUE_CONCRETE,
    ITEM_BROWN_CONCRETE,
    ITEM_GREEN_CONCRETE,
    ITEM_RED_CONCRETE,
    ITEM_BLACK_CONCRETE,
    ITEM_WHITE_CONCRETE_POWDER,
    ITEM_ORANGE_CONCRETE_POWDER,
    ITEM_MAGENTA_CONCRETE_POWDER,
    ITEM_LIGHT_BLUE_CONCRETE_POWDER,
    ITEM_YELLOW_CONCRETE_POWDER,
    ITEM_LIME_CONCRETE_POWDER,
    ITEM_PINK_CONCRETE_POWDER,
    ITEM_GRAY_CONCRETE_POWDER,
    ITEM_LIGHT_GRAY_CONCRETE_POWDER,
    ITEM_CYAN_CONCRETE_POWDER,
    ITEM_PURPLE_CONCRETE_POWDER,
    ITEM_BLUE_CONCRETE_POWDER,
    ITEM_BROWN_CONCRETE_POWDER,
    ITEM_GREEN_CONCRETE_POWDER,
    ITEM_RED_CONCRETE_POWDER,
    ITEM_BLACK_CONCRETE_POWDER,
    ITEM_TURTLE_EGG,
    ITEM_DEAD_TUBE_CORAL_BLOCK,
    ITEM_DEAD_BRAIN_CORAL_BLOCK,
    ITEM_DEAD_BUBBLE_CORAL_BLOCK,
    ITEM_DEAD_FIRE_CORAL_BLOCK,
    ITEM_DEAD_HORN_CORAL_BLOCK,
    ITEM_TUBE_CORAL_BLOCK,
    ITEM_BRAIN_CORAL_BLOCK,
    ITEM_BUBBLE_CORAL_BLOCK,
    ITEM_FIRE_CORAL_BLOCK,
    ITEM_HORN_CORAL_BLOCK,
    ITEM_TUBE_CORAL,
    ITEM_BRAIN_CORAL,
    ITEM_BUBBLE_CORAL,
    ITEM_FIRE_CORAL,
    ITEM_HORN_CORAL,
    ITEM_DEAD_BRAIN_CORAL,
    ITEM_DEAD_BUBBLE_CORAL,
    ITEM_DEAD_FIRE_CORAL,
    ITEM_DEAD_HORN_CORAL,
    ITEM_DEAD_TUBE_CORAL,
    ITEM_TUBE_CORAL_FAN,
    ITEM_BRAIN_CORAL_FAN,
    ITEM_BUBBLE_CORAL_FAN,
    ITEM_FIRE_CORAL_FAN,
    ITEM_HORN_CORAL_FAN,
    ITEM_DEAD_TUBE_CORAL_FAN,
    ITEM_DEAD_BRAIN_CORAL_FAN,
    ITEM_DEAD_BUBBLE_CORAL_FAN,
    ITEM_DEAD_FIRE_CORAL_FAN,
    ITEM_DEAD_HORN_CORAL_FAN,
    ITEM_BLUE_ICE,
    ITEM_CONDUIT,
    ITEM_POLISHED_GRANITE_STAIRS,
    ITEM_SMOOTH_RED_SANDSTONE_STAIRS,
    ITEM_MOSSY_STONE_BRICK_STAIRS,
    ITEM_POLISHED_DIORITE_STAIRS,
    ITEM_MOSSY_COBBLESTONE_STAIRS,
    ITEM_END_STONE_BRICK_STAIRS,
    ITEM_STONE_STAIRS,
    ITEM_SMOOTH_SANDSTONE_STAIRS,
    ITEM_SMOOTH_QUARTZ_STAIRS,
    ITEM_GRANITE_STAIRS,
    ITEM_ANDESITE_STAIRS,
    ITEM_RED_NETHER_BRICK_STAIRS,
    ITEM_POLISHED_ANDESITE_STAIRS,
    ITEM_DIORITE_STAIRS,
    ITEM_POLISHED_GRANITE_SLAB,
    ITEM_SMOOTH_RED_SANDSTONE_SLAB,
    ITEM_MOSSY_STONE_BRICK_SLAB,
    ITEM_POLISHED_DIORITE_SLAB,
    ITEM_MOSSY_COBBLESTONE_SLAB,
    ITEM_END_STONE_BRICK_SLAB,
    ITEM_SMOOTH_SANDSTONE_SLAB,
    ITEM_SMOOTH_QUARTZ_SLAB,
    ITEM_GRANITE_SLAB,
    ITEM_ANDESITE_SLAB,
    ITEM_RED_NETHER_BRICK_SLAB,
    ITEM_POLISHED_ANDESITE_SLAB,
    ITEM_DIORITE_SLAB,
    ITEM_SCAFFOLDING,
    ITEM_IRON_DOOR,
    ITEM_OAK_DOOR,
    ITEM_SPRUCE_DOOR,
    ITEM_BIRCH_DOOR,
    ITEM_JUNGLE_DOOR,
    ITEM_ACACIA_DOOR,
    ITEM_DARK_OAK_DOOR,
    ITEM_CRIMSON_DOOR,
    ITEM_WARPED_DOOR,
    ITEM_REPEATER,
    ITEM_COMPARATOR,
    ITEM_STRUCTURE_BLOCK,
    ITEM_JIGSAW,
    ITEM_TURTLE_HELMET,
    ITEM_SCUTE,
    ITEM_IRON_SHOVEL,
    ITEM_IRON_PICKAXE,
    ITEM_IRON_AXE,
    ITEM_FLINT_AND_STEEL,
    ITEM_APPLE,
    ITEM_BOW,
    ITEM_ARROW,
    ITEM_COAL,
    ITEM_CHARCOAL,
    ITEM_DIAMOND,
    ITEM_IRON_INGOT,
    ITEM_GOLD_INGOT,
    ITEM_NETHERITE_INGOT,
    ITEM_NETHERITE_SCRAP,
    ITEM_IRON_SWORD,
    ITEM_WOODEN_SWORD,
    ITEM_WOODEN_SHOVEL,
    ITEM_WOODEN_PICKAXE,
    ITEM_WOODEN_AXE,
    ITEM_STONE_SWORD,
    ITEM_STONE_SHOVEL,
    ITEM_STONE_PICKAXE,
    ITEM_STONE_AXE,
    ITEM_DIAMOND_SWORD,
    ITEM_DIAMOND_SHOVEL,
    ITEM_DIAMOND_PICKAXE,
    ITEM_DIAMOND_AXE,
    ITEM_STICK,
    ITEM_BOWL,
    ITEM_MUSHROOM_STEW,
    ITEM_GOLDEN_SWORD,
    ITEM_GOLDEN_SHOVEL,
    ITEM_GOLDEN_PICKAXE,
    ITEM_GOLDEN_AXE,
    ITEM_NETHERITE_SWORD,
    ITEM_NETHERITE_SHOVEL,
    ITEM_NETHERITE_PICKAXE,
    ITEM_NETHERITE_AXE,
    ITEM_STRING,
    ITEM_FEATHER,
    ITEM_GUNPOWDER,
    ITEM_WOODEN_HOE,
    ITEM_STONE_HOE,
    ITEM_IRON_HOE,
    ITEM_DIAMOND_HOE,
    ITEM_GOLDEN_HOE,
    ITEM_NETHERITE_HOE,
    ITEM_WHEAT_SEEDS,
    ITEM_WHEAT,
    ITEM_BREAD,
    ITEM_LEATHER_HELMET,
    ITEM_LEATHER_CHESTPLATE,
    ITEM_LEATHER_LEGGINGS,
    ITEM_LEATHER_BOOTS,
    ITEM_CHAINMAIL_HELMET,
    ITEM_CHAINMAIL_CHESTPLATE,
    ITEM_CHAINMAIL_LEGGINGS,
    ITEM_CHAINMAIL_BOOTS,
    ITEM_IRON_HELMET,
    ITEM_IRON_CHESTPLATE,
    ITEM_IRON_LEGGINGS,
    ITEM_IRON_BOOTS,
    ITEM_DIAMOND_HELMET,
    ITEM_DIAMOND_CHESTPLATE,
    ITEM_DIAMOND_LEGGINGS,
    ITEM_DIAMOND_BOOTS,
    ITEM_GOLDEN_HELMET,
    ITEM_GOLDEN_CHESTPLATE,
    ITEM_GOLDEN_LEGGINGS,
    ITEM_GOLDEN_BOOTS,
    ITEM_NETHERITE_HELMET,
    ITEM_NETHERITE_CHESTPLATE,
    ITEM_NETHERITE_LEGGINGS,
    ITEM_NETHERITE_BOOTS,
    ITEM_FLINT,
    ITEM_PORKCHOP,
    ITEM_COOKED_PORKCHOP,
    ITEM_PAINTING,
    ITEM_GOLDEN_APPLE,
    ITEM_ENCHANTED_GOLDEN_APPLE,
    ITEM_OAK_SIGN,
    ITEM_SPRUCE_SIGN,
    ITEM_BIRCH_SIGN,
    ITEM_JUNGLE_SIGN,
    ITEM_ACACIA_SIGN,
    ITEM_DARK_OAK_SIGN,
    ITEM_CRIMSON_SIGN,
    ITEM_WARPED_SIGN,
    ITEM_BUCKET,
    ITEM_WATER_BUCKET,
    ITEM_LAVA_BUCKET,
    ITEM_MINECART,
    ITEM_SADDLE,
    ITEM_REDSTONE,
    ITEM_SNOWBALL,
    ITEM_OAK_BOAT,
    ITEM_LEATHER,
    ITEM_MILK_BUCKET,
    ITEM_PUFFERFISH_BUCKET,
    ITEM_SALMON_BUCKET,
    ITEM_COD_BUCKET,
    ITEM_TROPICAL_FISH_BUCKET,
    ITEM_BRICK,
    ITEM_CLAY_BALL,
    ITEM_DRIED_KELP_BLOCK,
    ITEM_PAPER,
    ITEM_BOOK,
    ITEM_SLIME_BALL,
    ITEM_CHEST_MINECART,
    ITEM_FURNACE_MINECART,
    ITEM_EGG,
    ITEM_COMPASS,
    ITEM_FISHING_ROD,
    ITEM_CLOCK,
    ITEM_GLOWSTONE_DUST,
    ITEM_COD,
    ITEM_SALMON,
    ITEM_TROPICAL_FISH,
    ITEM_PUFFERFISH,
    ITEM_COOKED_COD,
    ITEM_COOKED_SALMON,
    ITEM_INK_SAC,
    ITEM_RED_DYE,
    ITEM_GREEN_DYE,
    ITEM_COCOA_BEANS,
    ITEM_LAPIS_LAZULI,
    ITEM_PURPLE_DYE,
    ITEM_CYAN_DYE,
    ITEM_LIGHT_GRAY_DYE,
    ITEM_GRAY_DYE,
    ITEM_PINK_DYE,
    ITEM_LIME_DYE,
    ITEM_YELLOW_DYE,
    ITEM_LIGHT_BLUE_DYE,
    ITEM_MAGENTA_DYE,
    ITEM_ORANGE_DYE,
    ITEM_BONE_MEAL,
    ITEM_BLUE_DYE,
    ITEM_BROWN_DYE,
    ITEM_BLACK_DYE,
    ITEM_WHITE_DYE,
    ITEM_BONE,
    ITEM_SUGAR,
    ITEM_CAKE,
    ITEM_WHITE_BED,
    ITEM_ORANGE_BED,
    ITEM_MAGENTA_BED,
    ITEM_LIGHT_BLUE_BED,
    ITEM_YELLOW_BED,
    ITEM_LIME_BED,
    ITEM_PINK_BED,
    ITEM_GRAY_BED,
    ITEM_LIGHT_GRAY_BED,
    ITEM_CYAN_BED,
    ITEM_PURPLE_BED,
    ITEM_BLUE_BED,
    ITEM_BROWN_BED,
    ITEM_GREEN_BED,
    ITEM_RED_BED,
    ITEM_BLACK_BED,
    ITEM_COOKIE,
    ITEM_FILLED_MAP,
    ITEM_SHEARS,
    ITEM_MELON_SLICE,
    ITEM_DRIED_KELP,
    ITEM_PUMPKIN_SEEDS,
    ITEM_MELON_SEEDS,
    ITEM_BEEF,
    ITEM_COOKED_BEEF,
    ITEM_CHICKEN,
    ITEM_COOKED_CHICKEN,
    ITEM_ROTTEN_FLESH,
    ITEM_ENDER_PEARL,
    ITEM_BLAZE_ROD,
    ITEM_GHAST_TEAR,
    ITEM_GOLD_NUGGET,
    ITEM_NETHER_WART,
    ITEM_POTION,
    ITEM_GLASS_BOTTLE,
    ITEM_SPIDER_EYE,
    ITEM_FERMENTED_SPIDER_EYE,
    ITEM_BLAZE_POWDER,
    ITEM_MAGMA_CREAM,
    ITEM_BREWING_STAND,
    ITEM_CAULDRON,
    ITEM_ENDER_EYE,
    ITEM_GLISTERING_MELON_SLICE,
    ITEM_BAT_SPAWN_EGG,
    ITEM_BEE_SPAWN_EGG,
    ITEM_BLAZE_SPAWN_EGG,
    ITEM_CAT_SPAWN_EGG,
    ITEM_CAVE_SPIDER_SPAWN_EGG,
    ITEM_CHICKEN_SPAWN_EGG,
    ITEM_COD_SPAWN_EGG,
    ITEM_COW_SPAWN_EGG,
    ITEM_CREEPER_SPAWN_EGG,
    ITEM_DOLPHIN_SPAWN_EGG,
    ITEM_DONKEY_SPAWN_EGG,
    ITEM_DROWNED_SPAWN_EGG,
    ITEM_ELDER_GUARDIAN_SPAWN_EGG,
    ITEM_ENDERMAN_SPAWN_EGG,
    ITEM_ENDERMITE_SPAWN_EGG,
    ITEM_EVOKER_SPAWN_EGG,
    ITEM_FOX_SPAWN_EGG,
    ITEM_GHAST_SPAWN_EGG,
    ITEM_GUARDIAN_SPAWN_EGG,
    ITEM_HOGLIN_SPAWN_EGG,
    ITEM_HORSE_SPAWN_EGG,
    ITEM_HUSK_SPAWN_EGG,
    ITEM_LLAMA_SPAWN_EGG,
    ITEM_MAGMA_CUBE_SPAWN_EGG,
    ITEM_MOOSHROOM_SPAWN_EGG,
    ITEM_MULE_SPAWN_EGG,
    ITEM_OCELOT_SPAWN_EGG,
    ITEM_PANDA_SPAWN_EGG,
    ITEM_PARROT_SPAWN_EGG,
    ITEM_PHANTOM_SPAWN_EGG,
    ITEM_PIG_SPAWN_EGG,
    ITEM_PIGLIN_SPAWN_EGG,
    ITEM_PILLAGER_SPAWN_EGG,
    ITEM_POLAR_BEAR_SPAWN_EGG,
    ITEM_PUFFERFISH_SPAWN_EGG,
    ITEM_RABBIT_SPAWN_EGG,
    ITEM_RAVAGER_SPAWN_EGG,
    ITEM_SALMON_SPAWN_EGG,
    ITEM_SHEEP_SPAWN_EGG,
    ITEM_SHULKER_SPAWN_EGG,
    ITEM_SILVERFISH_SPAWN_EGG,
    ITEM_SKELETON_SPAWN_EGG,
    ITEM_SKELETON_HORSE_SPAWN_EGG,
    ITEM_SLIME_SPAWN_EGG,
    ITEM_SPIDER_SPAWN_EGG,
    ITEM_SQUID_SPAWN_EGG,
    ITEM_STRAY_SPAWN_EGG,
    ITEM_STRIDER_SPAWN_EGG,
    ITEM_TRADER_LLAMA_SPAWN_EGG,
    ITEM_TROPICAL_FISH_SPAWN_EGG,
    ITEM_TURTLE_SPAWN_EGG,
    ITEM_VEX_SPAWN_EGG,
    ITEM_VILLAGER_SPAWN_EGG,
    ITEM_VINDICATOR_SPAWN_EGG,
    ITEM_WANDERING_TRADER_SPAWN_EGG,
    ITEM_WITCH_SPAWN_EGG,
    ITEM_WITHER_SKELETON_SPAWN_EGG,
    ITEM_WOLF_SPAWN_EGG,
    ITEM_ZOGLIN_SPAWN_EGG,
    ITEM_ZOMBIE_SPAWN_EGG,
    ITEM_ZOMBIE_HORSE_SPAWN_EGG,
    ITEM_ZOMBIE_VILLAGER_SPAWN_EGG,
    ITEM_ZOMBIFIED_PIGLIN_SPAWN_EGG,
    ITEM_EXPERIENCE_BOTTLE,
    ITEM_FIRE_CHARGE,
    ITEM_WRITABLE_BOOK,
    ITEM_WRITTEN_BOOK,
    ITEM_EMERALD,
    ITEM_ITEM_FRAME,
    ITEM_FLOWER_POT,
    ITEM_CARROT,
    ITEM_POTATO,
    ITEM_BAKED_POTATO,
    ITEM_POISONOUS_POTATO,
    ITEM_MAP,
    ITEM_GOLDEN_CARROT,
    ITEM_SKELETON_SKULL,
    ITEM_WITHER_SKELETON_SKULL,
    ITEM_PLAYER_HEAD,
    ITEM_ZOMBIE_HEAD,
    ITEM_CREEPER_HEAD,
    ITEM_DRAGON_HEAD,
    ITEM_CARROT_ON_A_STICK,
    ITEM_WARPED_FUNGUS_ON_A_STICK,
    ITEM_NETHER_STAR,
    ITEM_PUMPKIN_PIE,
    ITEM_FIREWORK_ROCKET,
    ITEM_FIREWORK_STAR,
    ITEM_ENCHANTED_BOOK,
    ITEM_NETHER_BRICK,
    ITEM_QUARTZ,
    ITEM_TNT_MINECART,
    ITEM_HOPPER_MINECART,
    ITEM_PRISMARINE_SHARD,
    ITEM_PRISMARINE_CRYSTALS,
    ITEM_RABBIT,
    ITEM_COOKED_RABBIT,
    ITEM_RABBIT_STEW,
    ITEM_RABBIT_FOOT,
    ITEM_RABBIT_HIDE,
    ITEM_ARMOR_STAND,
    ITEM_IRON_HORSE_ARMOR,
    ITEM_GOLDEN_HORSE_ARMOR,
    ITEM_DIAMOND_HORSE_ARMOR,
    ITEM_LEATHER_HORSE_ARMOR,
    ITEM_LEAD,
    ITEM_NAME_TAG,
    ITEM_COMMAND_BLOCK_MINECART,
    ITEM_MUTTON,
    ITEM_COOKED_MUTTON,
    ITEM_WHITE_BANNER,
    ITEM_ORANGE_BANNER,
    ITEM_MAGENTA_BANNER,
    ITEM_LIGHT_BLUE_BANNER,
    ITEM_YELLOW_BANNER,
    ITEM_LIME_BANNER,
    ITEM_PINK_BANNER,
    ITEM_GRAY_BANNER,
    ITEM_LIGHT_GRAY_BANNER,
    ITEM_CYAN_BANNER,
    ITEM_PURPLE_BANNER,
    ITEM_BLUE_BANNER,
    ITEM_BROWN_BANNER,
    ITEM_GREEN_BANNER,
    ITEM_RED_BANNER,
    ITEM_BLACK_BANNER,
    ITEM_END_CRYSTAL,
    ITEM_CHORUS_FRUIT,
    ITEM_POPPED_CHORUS_FRUIT,
    ITEM_BEETROOT,
    ITEM_BEETROOT_SEEDS,
    ITEM_BEETROOT_SOUP,
    ITEM_DRAGON_BREATH,
    ITEM_SPLASH_POTION,
    ITEM_SPECTRAL_ARROW,
    ITEM_TIPPED_ARROW,
    ITEM_LINGERING_POTION,
    ITEM_SHIELD,
    ITEM_ELYTRA,
    ITEM_SPRUCE_BOAT,
    ITEM_BIRCH_BOAT,
    ITEM_JUNGLE_BOAT,
    ITEM_ACACIA_BOAT,
    ITEM_DARK_OAK_BOAT,
    ITEM_TOTEM_OF_UNDYING,
    ITEM_SHULKER_SHELL,
    ITEM_IRON_NUGGET,
    ITEM_KNOWLEDGE_BOOK,
    ITEM_DEBUG_STICK,
    ITEM_MUSIC_DISC_13,
    ITEM_MUSIC_DISC_CAT,
    ITEM_MUSIC_DISC_BLOCKS,
    ITEM_MUSIC_DISC_CHIRP,
    ITEM_MUSIC_DISC_FAR,
    ITEM_MUSIC_DISC_MALL,
    ITEM_MUSIC_DISC_MELLOHI,
    ITEM_MUSIC_DISC_STAL,
    ITEM_MUSIC_DISC_STRAD,
    ITEM_MUSIC_DISC_WARD,
    ITEM_MUSIC_DISC_11,
    ITEM_MUSIC_DISC_WAIT,
    ITEM_MUSIC_DISC_PIGSTEP,
    ITEM_TRIDENT,
    ITEM_PHANTOM_MEMBRANE,
    ITEM_NAUTILUS_SHELL,
    ITEM_HEART_OF_THE_SEA,
    ITEM_CROSSBOW,
    ITEM_SUSPICIOUS_STEW,
    ITEM_LOOM,
    ITEM_FLOWER_BANNER_PATTERN,
    ITEM_CREEPER_BANNER_PATTERN,
    ITEM_SKULL_BANNER_PATTERN,
    ITEM_MOJANG_BANNER_PATTERN,
    ITEM_GLOBE_BANNER_PATTERN,
    ITEM_PIGLIN_BANNER_PATTERN,
    ITEM_COMPOSTER,
    ITEM_BARREL,
    ITEM_SMOKER,
    ITEM_BLAST_FURNACE,
    ITEM_CARTOGRAPHY_TABLE,
    ITEM_FLETCHING_TABLE,
    ITEM_GRINDSTONE,
    ITEM_LECTERN,
    ITEM_SMITHING_TABLE,
    ITEM_STONECUTTER,
    ITEM_BELL,
    ITEM_LANTERN,
    ITEM_SOUL_LANTERN,
    ITEM_SWEET_BERRIES,
    ITEM_CAMPFIRE,
    ITEM_SOUL_CAMPFIRE,
    ITEM_SHROOMLIGHT,
    ITEM_HONEYCOMB,
    ITEM_BEE_NEST,
    ITEM_BEEHIVE,
    ITEM_HONEY_BOTTLE,
    ITEM_HONEY_BLOCK,
    ITEM_HONEYCOMB_BLOCK,
    ITEM_LODESTONE,
    ITEM_NETHERITE_BLOCK,
    ITEM_ANCIENT_DEBRIS,
    ITEM_TARGET,
    ITEM_CRYING_OBSIDIAN,
    ITEM_BLACKSTONE,
    ITEM_BLACKSTONE_SLAB,
    ITEM_BLACKSTONE_STAIRS,
    ITEM_GILDED_BLACKSTONE,
    ITEM_POLISHED_BLACKSTONE,
    ITEM_POLISHED_BLACKSTONE_SLAB,
    ITEM_POLISHED_BLACKSTONE_STAIRS,
    ITEM_CHISELED_POLISHED_BLACKSTONE,
    ITEM_POLISHED_BLACKSTONE_BRICKS,
    ITEM_POLISHED_BLACKSTONE_BRICK_SLAB,
    ITEM_POLISHED_BLACKSTONE_BRICK_STAIRS,
    ITEM_CRACKED_POLISHED_BLACKSTONE_BRICKS,
    ITEM_RESPAWN_ANCHOR,
    ITEM_TYPE_COUNT,
};

typedef struct {
    // @NOTE(traks) zero size if and only if type is AIR, because that seems
    // what may be expected
    mc_int type;
    mc_ubyte size;
} item_stack;

// in network id order
enum entity_type {
    ENTITY_AREA_EFFECT_CLOUD,
    ENTITY_ARMOR_STAND,
    ENTITY_ARROW,
    ENTITY_BAT,
    ENTITY_BEE,
    ENTITY_BLAZE,
    ENTITY_BOAT,
    ENTITY_CAT,
    ENTITY_CAVE_SPIDER,
    ENTITY_CHICKEN,
    ENTITY_COD,
    ENTITY_COW,
    ENTITY_CREEPER,
    ENTITY_DOLPHIN,
    ENTITY_DONKEY,
    ENTITY_DRAGON_FIREBALL,
    ENTITY_DROWNED,
    ENTITY_ELDER_GUARDIAN,
    ENTITY_END_CRYSTAL,
    ENTITY_ENDER_DRAGON,
    ENTITY_ENDERMAN,
    ENTITY_ENDERMITE,
    ENTITY_EVOKER,
    ENTITY_EVOKER_FANGS,
    ENTITY_EXPERIENCE_ORB,
    ENTITY_EYE_OF_ENDER,
    ENTITY_FALLING_BLOCK,
    ENTITY_FIREWORK_ROCKET,
    ENTITY_FOX,
    ENTITY_GHAST,
    ENTITY_GIANT,
    ENTITY_GUARDIAN,
    ENTITY_HOGLIN,
    ENTITY_HORSE,
    ENTITY_HUSK,
    ENTITY_ILLUSIONER,
    ENTITY_IRON_GOLEM,
    ENTITY_ITEM,
    ENTITY_ITEM_FRAME,
    ENTITY_FIREBALL,
    ENTITY_LEASH_KNOT,
    ENTITY_LIGHTNING_BOLT,
    ENTITY_LLAMA,
    ENTITY_LLAMA_SPIT,
    ENTITY_MAGMA_CUBE,
    ENTITY_MINECART,
    ENTITY_CHEST_MINECART,
    ENTITY_COMMAND_BLOCK_MINECART,
    ENTITY_FURNACE_MINECART,
    ENTITY_HOPPER_MINECART,
    ENTITY_SPAWNER_MINECART,
    ENTITY_TNT_MINECART,
    ENTITY_MULE,
    ENTITY_MOOSHROOM,
    ENTITY_OCELOT,
    ENTITY_PAINTING,
    ENTITY_PANDA,
    ENTITY_PARROT,
    ENTITY_PHANTOM,
    ENTITY_PIG,
    ENTITY_PIGLIN,
    ENTITY_PIGLIN_BRUTE,
    ENTITY_PILLAGER,
    ENTITY_POLAR_BEAR,
    ENTITY_TNT,
    ENTITY_PUFFERFISH,
    ENTITY_RABBIT,
    ENTITY_RAVAGER,
    ENTITY_SALMON,
    ENTITY_SHEEP,
    ENTITY_SHULKER,
    ENTITY_SHULKER_BULLET,
    ENTITY_SILVERFISH,
    ENTITY_SKELETON,
    ENTITY_SKELETON_HORSE,
    ENTITY_SLIME,
    ENTITY_SMALL_FIREBALL,
    ENTITY_SNOW_GOLEM,
    ENTITY_SNOWBALL,
    ENTITY_SPECTRAL_ARROW,
    ENTITY_SPIDER,
    ENTITY_SQUID,
    ENTITY_STRAY,
    ENTITY_STRIDER,
    ENTITY_EGG,
    ENTITY_ENDER_PEARL,
    ENTITY_EXPERIENCE_BOTTLE,
    ENTITY_POTION,
    ENTITY_TRIDENT,
    ENTITY_TRADER_LLAMA,
    ENTITY_TROPICAL_FISH,
    ENTITY_TURTLE,
    ENTITY_VEX,
    ENTITY_VILLAGER,
    ENTITY_VINDICATOR,
    ENTITY_WANDERING_TRADER,
    ENTITY_WITCH,
    ENTITY_WITHER,
    ENTITY_WITHER_SKELETON,
    ENTITY_WITHER_SKULL,
    ENTITY_WOLF,
    ENTITY_ZOGLIN,
    ENTITY_ZOMBIE,
    ENTITY_ZOMBIE_HORSE,
    ENTITY_ZOMBIE_VILLAGER,
    ENTITY_ZOMBIFIED_PIGLIN,
    ENTITY_PLAYER,
    ENTITY_FISHING_BOBBER,
    ENTITY_NULL, // not used in vanilla
};

#define ENTITY_INDEX_MASK (MAX_ENTITIES - 1)

// Top 12 bits are used for the generation, lowest 20 bits can be used for the
// index into the entity table. Bits actually used for the index depends on
// MAX_ENTITIES.
static_assert(MAX_ENTITIES <= (1UL << 20), "MAX_ENTITIES too large");
typedef mc_uint entity_id;

// Player inventory slots are indexed as follows:
//
//  0           the crafting grid result slot
//  1-4         the 2x2 crafting grid slots
//  5-8         the 4 armour slots
//  9-35        the 36 main inventory slots
//  36-44       hotbar slots
//  45          off hand slot
//
// Here are some defines for convenience.
#define PLAYER_SLOTS (46)
#define PLAYER_FIRST_HOTBAR_SLOT (36)
#define PLAYER_LAST_HOTBAR_SLOT (44)
#define PLAYER_OFF_HAND_SLOT (45)

typedef struct {
    unsigned char username[16];
    int username_size;

    item_stack slots_prev_tick[PLAYER_SLOTS];
    item_stack slots[PLAYER_SLOTS];
    static_assert(PLAYER_SLOTS <= 64, "Too many player slots");
    mc_ulong slots_needing_update;
    unsigned char selected_slot;

    unsigned char gamemode;

    // @NOTE(traks) the server doesn't tell clients the body rotation of
    // players. The client determines the body rotation based on the player's
    // movement and their head rotation. However, we do need to send a players
    // head rotation using the designated packet, otherwise heads won't rotate.
    // @NOTE(traks) these values shouldn't exceed the range [0, 360] by too
    // much, otherwise float -> integer conversion errors may occur.
    float head_rot_x;
    float head_rot_y;
} player_data;

typedef struct {
    item_stack contents;
} item_data;

#define ENTITY_IN_USE ((unsigned) (1 << 0))

#define ENTITY_TELEPORTING ((unsigned) (1 << 1))

#define ENTITY_ON_GROUND ((unsigned) (1 << 2))

typedef struct {
    double x;
    double y;
    double z;
    entity_id eid;
    unsigned flags;
    unsigned type;

    union {
        player_data player;
        item_data item;
    };
} entity_data;

#define PLAYER_BRAIN_IN_USE ((unsigned) (1 << 0))

#define PLAYER_BRAIN_DID_INIT_PACKETS ((unsigned) (1 << 1))

#define PLAYER_BRAIN_SENT_TELEPORT ((unsigned) (1 << 2))

#define PLAYER_BRAIN_GOT_ALIVE_RESPONSE ((unsigned) (1 << 3))

#define PLAYER_BRAIN_SHIFTING ((unsigned) (1 << 4))

#define PLAYER_BRAIN_SPRINTING ((unsigned) (1 << 5))

#define PLAYER_BRAIN_INITIALISED_TAB_LIST ((unsigned) (1 << 6))

typedef struct {
    unsigned char sent;
} chunk_cache_entry;

typedef struct {
    int sock;
    unsigned flags;
    unsigned char rec_buf[65536];
    int rec_cursor;

    unsigned char send_buf[1048576];
    int send_cursor;

    // The radius of the client's view distance, excluding the centre chunk,
    // and including an extra outer rim the client doesn't render but uses
    // for connected blocks and such.
    int chunk_cache_radius;
    mc_short chunk_cache_centre_x;
    mc_short chunk_cache_centre_z;
    int new_chunk_cache_radius;
    // @TODO(traks) maybe this should just be a bitmap
    chunk_cache_entry chunk_cache[MAX_CHUNK_CACHE_DIAM * MAX_CHUNK_CACHE_DIAM];

    mc_int current_teleport_id;

    unsigned char language[16];
    int language_size;
    mc_int chat_visibility;
    mc_ubyte sees_chat_colours;
    mc_ubyte model_customisation;
    mc_int main_hand;

    mc_ulong last_keep_alive_sent_tick;

    entity_id eid;

    // @TODO(traks) this feels a bit silly, but very simple
    entity_id tracked_entities[MAX_ENTITIES];

    net_block_pos changed_blocks[8];
    mc_ubyte changed_block_count;
} player_brain;

typedef struct {
    mc_ushort size;
    unsigned char text[512];
} global_msg;

typedef struct {
    entity_id eid;
} tab_list_entry;

typedef struct {
    // index into string buffer for name size + value
    int name_index;
    int value_count;
    // index into value id buffer for array of values
    int values_index;
} tag_spec;

typedef struct {
    int size;
    tag_spec tags[128];
} tag_list;

#define RESOURCE_LOC_MAX_SIZE (256)

typedef struct {
    unsigned char size;
    mc_ushort id;
    mc_uint buf_index;
} resource_loc_entry;

typedef struct {
    mc_int size_mask;
    mc_int string_buf_size;
    resource_loc_entry * entries;
    unsigned char * string_buf;
    mc_int last_string_buf_index;
} resource_loc_table;

// Currently dimension types have the following configurable properties that are
// shared with the client. These have the effects:
//
//  - fixed_time (optional long): time of day always equals this
//  - has_skylight (bool): sky light levels, whether it can
//    thunder, whether daylight sensors work, phantom spawning
//  - has_ceiling (bool): affects thunder, map rendering, mob
//    spawning algorithm, respawn logic
//  - ultrawarm (bool): whether water can be placed, affects ice
//    melting, and how far and how fast lava flows
//  - natural (bool): whether players can sleep and whether
//    zombified piglin can spawn from portals
//  - coordinate_scale (double): vanilla overworld has 1 and vanilla
//    nether has 8. affects teleporting between worlds
//  - piglin_safe (bool): false if piglins convert to zombified
//    piglins as in the vanilla overworld
//  - bed_works (bool): true if beds can set spawn point. else beds will
//    explode when used
//  - respawn_anchor_works (bool): true if respawn anchors can
//    set spawn point. else they explode when used
//  - has_raids (bool): whether raids spawn
//  - logical_height (int in [0, 256]): seems to only affect
//    chorus fruit teleportation and nether portal spawning, not
//    the actual maximum world height
//  - infiniburn (resource loc): the resource location of a
//    block tag that is used to check whether fire should keep
//    burning forever on tagged blocks
//  - effects (resource loc): affects cloud height, fog, sky colour, etc. for
//    the client
//  - ambient_light (float): affects brightness visually and some mob AI

#define DIMENSION_HAS_SKYLIGHT ((unsigned) (0x1 << 0))

#define DIMENSION_HAS_CEILING ((unsigned) (0x1 << 1))

#define DIMENSION_ULTRAWARM ((unsigned) (0x1 << 2))

#define DIMENSION_NATURAL ((unsigned) (0x1 << 3))

#define DIMENSION_PIGLIN_SAFE ((unsigned) (0x1 << 4))

#define DIMENSION_BED_WORKS ((unsigned) (0x1 << 5))

#define DIMENSION_RESPAWN_ANCHOR_WORKS ((unsigned) (0x1 << 6))

#define DIMENSION_HAS_RAIDS ((unsigned) (0x1 << 7))

typedef struct {
    unsigned char name[64];
    unsigned char name_size;
    mc_long fixed_time; // -1 if not used
    unsigned flags;
    double coordinate_scale;
    mc_int logical_height;
    unsigned char infiniburn[128];
    unsigned char infiniburn_size;
    unsigned char effects[128];
    unsigned char effects_size;
    float ambient_light;
} dimension_type;

enum biome_precipitation {
    BIOME_PRECIPITATION_NONE,
    BIOME_PRECIPITATION_RAIN,
    BIOME_PRECIPITATION_SNOW,
};

enum biome_category {
    BIOME_CATEGORY_NONE,
    BIOME_CATEGORY_TAIGA,
    BIOME_CATEGORY_EXTREME_HILLS,
    BIOME_CATEGORY_JUNGLE,
    BIOME_CATEGORY_MESA,
    BIOME_CATEGORY_PLAINS,
    BIOME_CATEGORY_SAVANNA,
    BIOME_CATEGORY_ICY,
    BIOME_CATEGORY_THE_END,
    BIOME_CATEGORY_BEACH,
    BIOME_CATEGORY_FOREST,
    BIOME_CATEGORY_OCEAN,
    BIOME_CATEGORY_DESERT,
    BIOME_CATEGORY_RIVER,
    BIOME_CATEGORY_SWAMP,
    BIOME_CATEGORY_MUSHROOM,
    BIOME_CATEGORY_NETHER,
};

enum biome_temperature_modifier {
    BIOME_TEMPERATURE_MOD_NONE,
    BIOME_TEMPERATURE_MOD_FROZEN,
};

enum biome_grass_colour_modifier {
    BIOME_GRASS_COLOUR_MOD_NONE,
    BIOME_GRASS_COLOUR_MOD_DARK_FOREST,
    BIOME_GRASS_COLOUR_MOD_SWAMP,
};

// @TODO(traks) description of all fields in the biome struct

typedef struct {
    unsigned char name[64];
    unsigned char name_size;
    unsigned char precipitation;
    unsigned char category;
    float temperature;
    float downfall;
    unsigned temperature_mod;
    float depth;
    float scale;

    mc_int fog_colour;
    mc_int water_colour;
    mc_int water_fog_colour;
    mc_int sky_colour;
    mc_int foliage_colour_override; // -1 if not used
    mc_int grass_colour_override; // -1 if not used
    unsigned char grass_colour_mod;

    // @TODO(traks) complex ambient particle settings

    unsigned char ambient_sound[64];
    unsigned char ambient_sound_size;

    unsigned char mood_sound[64];
    unsigned char mood_sound_size;
    mc_int mood_sound_tick_delay;
    mc_int mood_sound_block_search_extent;
    double mood_sound_offset;

    unsigned char additions_sound[64];
    unsigned char additions_sound_size;
    double additions_sound_tick_chance;

    unsigned char music_sound[64];
    unsigned char music_sound_size;
    mc_int music_min_delay;
    mc_int music_max_delay;
    mc_ubyte music_replace_current_music;
} biome;

typedef struct {
    unsigned long long current_tick;

    entity_data entities[MAX_ENTITIES];
    mc_ushort next_entity_generations[MAX_ENTITIES];
    mc_int entity_count;

    player_brain player_brains[MAX_PLAYERS];
    int player_brain_count;

    // All chunks that should be loaded. Stored in a request list to allow for
    // ordered loads. If a
    // @TODO(traks) appropriate size
    chunk_pos chunk_load_requests[64];
    int chunk_load_request_count;

    // global messages for the current tick
    global_msg global_msgs[16];
    int global_msg_count;

    void * short_lived_scratch;
    mc_int short_lived_scratch_size;

    tab_list_entry tab_list_added[64];
    int tab_list_added_count;
    tab_list_entry tab_list_removed[64];
    int tab_list_removed_count;
    tab_list_entry tab_list[MAX_PLAYERS];
    int tab_list_size;

    tag_list block_tags;
    tag_list entity_tags;
    tag_list fluid_tags;
    tag_list item_tags;
    int tag_name_count;
    int tag_value_id_count;
    unsigned char tag_name_buf[1 << 12];
    mc_ushort tag_value_id_buf[1 << 12];

    resource_loc_table block_resource_table;
    resource_loc_table item_resource_table;
    resource_loc_table entity_resource_table;
    resource_loc_table fluid_resource_table;

    dimension_type dimension_types[32];
    int dimension_type_count;

    biome biomes[128];
    int biome_count;
} server;

// in network order
enum player_hand {
    PLAYER_MAIN_HAND,
    PLAYER_OFF_HAND,
};

void
logs(void * format, ...);

void
logs_errno(void * format);

void *
alloc_in_arena(memory_arena * arena, mc_int size);

mc_int
net_read_varint(buffer_cursor * cursor);

void
net_write_varint(buffer_cursor * cursor, mc_int val);

int
net_varint_size(mc_int val);

mc_long
net_read_varlong(buffer_cursor * cursor);

void
net_write_varlong(buffer_cursor * cursor, mc_long val);

mc_int
net_read_int(buffer_cursor * cursor);

void
net_write_int(buffer_cursor * cursor, mc_int val);

mc_short
net_read_short(buffer_cursor * cursor);

void
net_write_short(buffer_cursor * cursor, mc_short val);

mc_byte
net_read_byte(buffer_cursor * cursor);

void
net_write_byte(buffer_cursor * cursor, mc_byte val);

mc_ushort
net_read_ushort(buffer_cursor * cursor);

void
net_write_ushort(buffer_cursor * cursor, mc_ushort val);

mc_ulong
net_read_ulong(buffer_cursor * cursor);

void
net_write_ulong(buffer_cursor * cursor, mc_ulong val);

mc_uint
net_read_uint(buffer_cursor * cursor);

void
net_write_uint(buffer_cursor * cursor, mc_uint val);

mc_ubyte
net_read_ubyte(buffer_cursor * cursor);

void
net_write_ubyte(buffer_cursor * cursor, mc_ubyte val);

net_string
net_read_string(buffer_cursor * cursor, mc_int max_size);

void
net_write_string(buffer_cursor * cursor, net_string val);

float
net_read_float(buffer_cursor * cursor);

void
net_write_float(buffer_cursor * cursor, float val);

double
net_read_double(buffer_cursor * cursor);

void
net_write_double(buffer_cursor * cursor, double val);

net_block_pos
net_read_block_pos(buffer_cursor * cursor);

void
net_write_block_pos(buffer_cursor * cursor, net_block_pos val);

void
net_write_data(buffer_cursor * cursor, void * restrict src, size_t size);

nbt_tape_entry *
nbt_move_to_key(net_string matcher, nbt_tape_entry * tape,
        buffer_cursor * cursor);

net_string
nbt_get_string(net_string matcher, nbt_tape_entry * tape,
        buffer_cursor * cursor);

nbt_tape_entry *
nbt_get_compound(net_string matcher, nbt_tape_entry * tape,
        buffer_cursor * cursor);

nbt_tape_entry *
load_nbt(buffer_cursor * cursor, memory_arena * arena, int max_level);

void
print_nbt(nbt_tape_entry * tape, buffer_cursor * cursor,
        memory_arena * arena, int max_levels);

void
begin_timed_block(char * name);

void
end_timed_block();

int
find_property_value_index(block_property_spec * prop_spec, net_string val);

chunk *
get_or_create_chunk(chunk_pos pos);

chunk *
get_chunk_if_loaded(chunk_pos pos);

chunk *
get_chunk_if_available(chunk_pos pos);

void
chunk_set_block_state(chunk * ch, int x, int y, int z, mc_ushort block_state);

mc_ushort
chunk_get_block_state(chunk * ch, int x, int y, int z);

void
try_read_chunk_from_storage(chunk_pos pos, chunk * ch,
        memory_arena * scratch_arena,
        block_properties * block_properties_table,
        block_property_spec * block_property_specs,
        resource_loc_table * block_resource_table);

chunk_section *
alloc_chunk_section(void);

void
free_chunk_section(chunk_section * section);

void
clean_up_unused_chunks(void);

entity_data *
resolve_entity(server * serv, entity_id eid);

entity_data *
try_reserve_entity(server * serv, unsigned type);

void
evict_entity(server * serv, entity_id eid);

void
teleport_player(player_brain * brain, entity_data * entity,
        double new_x, double new_y, double new_z,
        float new_rot_x, float new_rot_y);

void
tick_player_brain(player_brain * brain, server * serv,
        memory_arena * tick_arena);

void
send_packets_to_player(player_brain * brain, server * serv,
        memory_arena * tick_arena);

mc_short
resolve_resource_loc_id(net_string resource_loc, resource_loc_table * table);

int
net_string_equal(net_string a, net_string b);

void
process_use_item_on_packet(entity_data * entity, player_brain * brain,
        mc_int hand, net_block_pos clicked_pos, mc_int clicked_face,
        float click_offset_x, float click_offset_y, float click_offset_z,
        mc_ubyte is_inside);

mc_ubyte
get_max_stack_size(mc_int item_type);

#endif
