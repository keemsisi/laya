#pragma once
// Reads a router golden row (a Router's kwargs, a route's kwargs, a decision) into the C++ API and
// compares the answer. Shared by the GoogleTest suite and the differential fuzz CLI, so the fuzz
// holds the library to exactly what the goldens do. Returns "" when equal, else a description.
#include <string>
#include <vector>

#include "laya/pyjson.hpp"
#include "laya/router.hpp"

namespace router_check {

namespace pv = laya::pyvalue;
namespace rt = laya::router;

// "@callable" in a golden stands for this hint: the first word after a leading "@", else abstain.
inline std::string AtHint(const pv::Value& state) {
  if (state.kind() != pv::Value::Kind::String) return "";
  const std::string& s = state.as_string();
  if (s.empty() || s[0] != '@') return "";
  size_t sp = s.find(' ');
  return s.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
}
inline rt::LangHint HintOf(const std::string& v) {
  return v == "@callable" ? rt::LangHint(AtHint) : rt::LangCode(v);
}

inline rt::Options OptionsOf(const pv::Value& kw) {
  rt::Options o;
  for (size_t i = 0; i < kw.size(); i++) {
    const std::string& k = kw.key(i);
    const pv::Value& v = kw.value(i);
    if (k == "default") o.default_model = v.as_string();
    else if (k == "auto_task_detection") o.auto_task_detection = v.as_bool();
    else if (k == "standalone_repos") o.standalone_repos = v.as_bool();
    else if (k == "lang_guess") o.lang_guess = HintOf(v.as_string());
    else if (k == "models") {
      for (size_t j = 0; j < v.size(); j++) {
        const pv::Value& src = v.value(j);
        if (src.kind() == pv::Value::Kind::String) o.WithSource(v.key(j), src.as_string());
        else o.WithSourceIn(v.key(j), src.items()[0].as_string(),
                            src.items()[1].is_null() ? "" : src.items()[1].as_string());
      }
    }
  }
  return o;
}

inline rt::CallOptions CallOf(const pv::Value& kw) {
  rt::CallOptions c;
  for (size_t i = 0; i < kw.size(); i++) {
    const std::string& k = kw.key(i);
    const pv::Value& v = kw.value(i);
    if (k == "model") c.model = v.as_string();
    else if (k == "task") c.task = v.as_string();
    else if (k == "lang") c.lang = v.as_string();
    else if (k == "lang_guess") c.lang_guess = HintOf(v.as_string());
  }
  return c;
}

inline std::vector<std::string> IdsOf(const pv::Value* questions) {
  std::vector<std::string> ids;
  if (questions != nullptr && questions->kind() == pv::Value::Kind::Object)
    for (size_t i = 0; i < questions->size(); i++) ids.push_back(questions->key(i));
  return ids;
}

inline rt::Request RequestOf(const pv::Value& kw) {
  rt::Request r;
  const pv::Value* s = kw.Find("state");
  if (s != nullptr) r.state = *s;
  r.question_ids = IdsOf(kw.Find("questions"));
  r.options = CallOf(kw);
  return r;
}

inline std::string Str(const pv::Value* v) { return v == nullptr || v->is_null() ? "" : v->as_string(); }
inline double Num(const pv::Value& v) { return v.kind() == pv::Value::Kind::Int ? double(v.as_int()) : v.as_float(); }

inline std::string Diff(const rt::Decision& got, const pv::Value& want) {
  std::string out;
  auto field = [&](const char* name, const std::string& g, const std::string& w) {
    if (g != w) out += std::string(name) + " got " + laya::pyjson::Repr(g) + " want " + laya::pyjson::Repr(w) + "; ";
  };
  field("model", std::string(rt::Name(got.model)), Str(want.Find("model")));
  field("repo", got.repo, Str(want.Find("repo")));
  field("reason", got.reason, Str(want.Find("reason")));
  field("workflow", got.workflow, Str(want.Find("workflow")));
  const pv::Value* det = want.Find("detection");
  if (det == nullptr || det->is_null()) {
    if (got.detection) out += "detection present, want none; ";
    return out;
  }
  if (!got.detection) return out + "detection missing; ";
  const laya::lang::Analysis& g = *got.detection;
  field("script", g.script, Str(det->Find("script")));
  field("language", g.language, Str(det->Find("language")));
  field("mixed_segment", g.mixed_segment, Str(det->Find("mixed_segment")));
  if (g.english != det->Find("is_english")->as_bool()) out += "is_english; ";
  if ((g.language.empty()) != det->Find("language_undecided")->as_bool()) out += "language_undecided; ";
  if (g.non_latin_fraction != Num(*det->Find("non_latin_fraction"))) out += "non_latin_fraction; ";
  if (g.diacritic_rate != Num(*det->Find("diacritic_rate"))) out += "diacritic_rate; ";
  return out;
}

// One routes-family row (also a fuzz case): "" when the library agrees with the row.
inline std::string CheckRoute(const pv::Value& rec) {
  rt::Options opts = OptionsOf(*rec.Find("router"));
  laya::Result<rt::Router> r = rt::Router::Create(opts);
  if (const pv::Value* want = rec.Find("router_error")) {
    if (r.ok()) return "Create succeeded, want error " + want->as_string();
    return r.error() == want->as_string() ? "" : "Create error " + r.error() + " want " + want->as_string();
  }
  if (!r.ok()) return "Create failed: " + r.error();
  const pv::Value& route = *rec.Find("route");
  rt::Request req = RequestOf(route);
  laya::Result<rt::Decision> d = r.value().Route(req.state, req.question_ids, req.options);
  if (const pv::Value* want = rec.Find("error")) {
    if (d.ok()) return "Route succeeded, want error " + want->as_string();
    return d.error() == want->as_string() ? "" : "Route error " + d.error() + " want " + want->as_string();
  }
  if (!d.ok()) return "Route failed: " + d.error();
  return Diff(d.value(), *rec.Find("decision"));
}

}  // namespace router_check
