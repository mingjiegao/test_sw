/* src/test/modules/test_sw/test_sw--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION test_sw" to load this file. \quit



CREATE OR REPLACE FUNCTION test_sw_01() RETURNS VOID AS 'MODULE_PATHNAME', 'test_sw_01' LANGUAGE C;

CREATE OR REPLACE FUNCTION test_sw_02() RETURNS VOID AS 'MODULE_PATHNAME', 'test_sw_02' LANGUAGE C;

CREATE OR REPLACE FUNCTION test_sw_03() RETURNS VOID AS 'MODULE_PATHNAME', 'test_sw_03' LANGUAGE C;
