// Copyright 2026 MDJ. Use of this source code is governed by a BSD-style
// license that can be found in the Chromium LICENSE file.

//! Thin cxx bridge over Brave's `adblock` crate (adblock-rust).
//! Written against adblock 0.10.x; see the notes in README for API drift.

use adblock::lists::ParseOptions;
use adblock::request::Request;
use adblock::resources::Resource;
use adblock::{Engine, FilterSet};
use std::collections::HashSet;

pub struct AdblockEngine {
    engine: Engine,
}

#[cxx::bridge(namespace = "sidebar_apps::adblock::rs")]
mod ffi {
    #[derive(Default)]
    struct BlockResult {
        matched: bool,
        important: bool,
        exception: bool,
        // A data: URL to serve instead of the resource (uBO "redirect").
        redirect: String,
        // Set when a $removeparam rule rewrote the URL.
        rewritten_url: String,
    }

    #[derive(Default)]
    struct CosmeticResult {
        hide_selectors: Vec<String>,
        // Scriptlets. Needs a main-world, document-start injector (phase 2).
        injected_script: String,
        generichide: bool,
    }

    extern "Rust" {
        type AdblockEngine;
        fn engine_new(lists: Vec<String>, resources_json: String) -> Box<AdblockEngine>;
        fn engine_check(
            engine: &AdblockEngine,
            url: &str,
            source_url: &str,
            request_type: &str,
        ) -> BlockResult;
        fn engine_cosmetic(engine: &AdblockEngine, url: &str) -> CosmeticResult;
        fn engine_hidden_selectors(
            engine: &AdblockEngine,
            url: &str,
            classes: Vec<String>,
            ids: Vec<String>,
        ) -> Vec<String>;
    }
}

use ffi::{BlockResult, CosmeticResult};

fn engine_new(lists: Vec<String>, resources_json: String) -> Box<AdblockEngine> {
    // `false` = do not keep debug info about which rule matched (saves memory).
    let mut filter_set = FilterSet::new(false);
    for list in &lists {
        filter_set.add_filters(list.lines(), ParseOptions::default());
    }
    let mut engine = Engine::from_filter_set(filter_set, /* optimize = */ true);
    if !resources_json.is_empty() {
        if let Ok(resources) = serde_json::from_str::<Vec<Resource>>(&resources_json) {
            engine.use_resources(resources);
        }
    }
    Box::new(AdblockEngine { engine })
}

fn engine_check(
    engine: &AdblockEngine,
    url: &str,
    source_url: &str,
    request_type: &str,
) -> BlockResult {
    let request = match Request::new(url, source_url, request_type) {
        Ok(r) => r,
        Err(_) => return BlockResult::default(),
    };
    let r = engine.engine.check_network_request(&request);
    BlockResult {
        matched: r.matched,
        important: r.important,
        exception: r.exception.is_some(),
        redirect: r.redirect.unwrap_or_default(),
        rewritten_url: r.rewritten_url.unwrap_or_default(),
    }
}

fn engine_cosmetic(engine: &AdblockEngine, url: &str) -> CosmeticResult {
    let r = engine.engine.url_cosmetic_resources(url);
    CosmeticResult {
        hide_selectors: r.hide_selectors.into_iter().collect(),
        injected_script: r.injected_script,
        generichide: r.generichide,
    }
}

fn engine_hidden_selectors(
    engine: &AdblockEngine,
    url: &str,
    classes: Vec<String>,
    ids: Vec<String>,
) -> Vec<String> {
    let url_resources = engine.engine.url_cosmetic_resources(url);
    if url_resources.generichide {
        return Vec::new();
    }
    let exceptions: HashSet<String> = url_resources.exceptions;
    engine
        .engine
        .hidden_class_id_selectors(classes, ids, &exceptions)
}
