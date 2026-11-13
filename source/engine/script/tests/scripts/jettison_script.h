/* The host API for Jettison's scripts, generated from the reflection tables of the game that wrote
 * it (Script::Header): the imports a script may be granted, and the game's types a script sees,
 * laid out as the host copies them in and out of a script's memory. Do not edit it; write it again
 * from the game, which the sample shell does with its console's "script_bindings <directory>".
 *
 * A script is a wasm32 module, compiled freestanding: clang --target=wasm32 -ffreestanding
 * -nostdlib, linked with wasm-ld --no-entry. Its handlers and inputs are exported by name
 * (JETTISON_EXPORT), and its definition (defs/scripts/<name>.yaml) names them, with what it is
 * granted. A module that imports what it is not granted is refused as it loads. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JETTISON_IMPORT(name) __attribute__((import_module("jettison"), import_name(name)))
#define JETTISON_EXPORT(name) __attribute__((export_name(name)))

typedef uint32_t jettison_entity;
#define JETTISON_NO_ENTITY ((jettison_entity)0xffffffffu)

/* A string's bytes, not terminated, and their count. An event's strings are the host's copy, valid
 * for the handler's call. */
typedef struct jettison_string
{
	const char *data;
	uint32_t size;
} jettison_string;
#define JETTISON_STRING(literal) ((jettison_string){(literal), sizeof(literal) - 1})

typedef struct jettison_vec2 { float x, y; } jettison_vec2;
typedef struct jettison_vec3 { float x, y, z; } jettison_vec3;
typedef struct jettison_vec4 { float x, y, z, w; } jettison_vec4;
typedef struct jettison_dvec2 { double x, y; } jettison_dvec2;
typedef struct jettison_dvec3 { double x, y, z; } jettison_dvec3;
typedef struct jettison_dvec4 { double x, y, z, w; } jettison_dvec4;
typedef struct jettison_quat { float x, y, z, w; } jettison_quat;
typedef struct jettison_dquat { double x, y, z, w; } jettison_dquat;

enum
{
	JETTISON_DEBUG = 0,
	JETTISON_INFO = 1,
	JETTISON_WARNING = 2,
	JETTISON_ERROR = 3
};

/* Granted by "log": a line in the game's log, under the script's name. */
JETTISON_IMPORT("log") void jettison_log(int32_t severity, const char *text, uint32_t size);

/* For a script that answers inputs, and only while it answers one: the option's input of that name,
 * as the game works it out. A name the game does not know traps. */
JETTISON_IMPORT("input") double jettison_input(const char *name, uint32_t size);

/* Granted by "store": keeps the bytes under the key, for this script alone and in the game's saves,
 * in whatever encoding the script chooses; no bytes take the key away. A store past the script's
 * keys or bytes traps, as does one while it answers an input. */
JETTISON_IMPORT("store")
void jettison_store(const char *key, uint32_t key_size, const void *data, uint32_t size);
/* Granted by "store": copies what is kept under the key into the buffer, as much as fits, and returns
 * how many bytes are kept, or -1 for none. */
JETTISON_IMPORT("load")
int32_t jettison_load(const char *key, uint32_t key_size, void *buffer, uint32_t capacity);

typedef struct jettison_array_int32_t
{
	const int32_t *data;
	uint32_t size;
} jettison_array_int32_t;

/* Test.Mood */
typedef uint8_t Test_Mood;
#define Test_Mood_Calm ((Test_Mood)0u)
#define Test_Mood_Cross ((Test_Mood)1u)

/* Test.Inner */
typedef struct Test_Inner
{
	Test_Mood mood;
	int32_t weight;
} Test_Inner;
_Static_assert(sizeof(Test_Inner) == 8, "Test.Inner is laid out as the host lays it out");
_Static_assert(offsetof(Test_Inner, mood) == 0, "Test.Inner.mood");
_Static_assert(offsetof(Test_Inner, weight) == 4, "Test.Inner.weight");

/* Test.Ping */
typedef struct Test_Ping
{
	jettison_entity ship;
	int64_t count;
	double value;
	jettison_string text;
	jettison_array_int32_t numbers;
	Test_Inner inner;
	jettison_dvec3 where;
	bool flag;
	/* hidden (std::optional<int>): not seen by scripts */
} Test_Ping;
_Static_assert(sizeof(Test_Ping) == 80, "Test.Ping is laid out as the host lays it out");
_Static_assert(offsetof(Test_Ping, ship) == 0, "Test.Ping.ship");
_Static_assert(offsetof(Test_Ping, count) == 8, "Test.Ping.count");
_Static_assert(offsetof(Test_Ping, value) == 16, "Test.Ping.value");
_Static_assert(offsetof(Test_Ping, text) == 24, "Test.Ping.text");
_Static_assert(offsetof(Test_Ping, numbers) == 32, "Test.Ping.numbers");
_Static_assert(offsetof(Test_Ping, inner) == 40, "Test.Ping.inner");
_Static_assert(offsetof(Test_Ping, where) == 48, "Test.Ping.where");
_Static_assert(offsetof(Test_Ping, flag) == 72, "Test.Ping.flag");

typedef struct jettison_array_Test_Inner
{
	const Test_Inner *data;
	uint32_t size;
} jettison_array_Test_Inner;

/* Test.Pong */
typedef struct Test_Pong
{
	jettison_entity ship;
	int64_t total;
	jettison_string text;
	jettison_array_Test_Inner items;
} Test_Pong;
_Static_assert(sizeof(Test_Pong) == 32, "Test.Pong is laid out as the host lays it out");
_Static_assert(offsetof(Test_Pong, ship) == 0, "Test.Pong.ship");
_Static_assert(offsetof(Test_Pong, total) == 8, "Test.Pong.total");
_Static_assert(offsetof(Test_Pong, text) == 16, "Test.Pong.text");
_Static_assert(offsetof(Test_Pong, items) == 24, "Test.Pong.items");

/* Granted by "send:Test.Pong": the command, into the game's queue, applied at the start of the next
 * step and recorded with the player's. */
JETTISON_IMPORT("send:Test.Pong") void jettison_send_Test_Pong(const Test_Pong *command);

