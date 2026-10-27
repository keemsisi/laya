#include "laya/router.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

#if !defined(_WIN32)
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "laya/pyjson.hpp"
#include "laya/unicodedata.hpp"

namespace laya {
namespace router {
namespace {

struct Alias {
  const char* name;
  Checkpoint target;
};
// Sorted by name, so the error text lists them as sorted(_ALIASES) does.
constexpr Alias kAliases[] = {
    {"decisions", Checkpoint::TypedDecisions}, {"default", Checkpoint::English},
    {"en", Checkpoint::English},               {"laya", Checkpoint::English},
    {"laya-multilingual", Checkpoint::Multilingual},
    {"laya-typed-decisions", Checkpoint::TypedDecisions},
    {"ml", Checkpoint::Multilingual},          {"multi", Checkpoint::Multilingual},
    {"typed", Checkpoint::TypedDecisions},     {"typed_decisions", Checkpoint::TypedDecisions},
};

struct Workflow {
  const char* name;
  std::array<const char*, 5> ids;  // sorted
};
constexpr Workflow kWorkflows[] = {
    {"agent_trace_observability", {"action", "needs_review", "outcome", "risk", "urgency"}},
    {"customer_service", {"action", "category", "churn_risk", "needs_human", "urgency"}},
    {"invoice_processing", {"discrepancy_severity", "disposition", "duplicate", "matches_order", "urgency"}},
    {"security_incidents", {"credential_compromise", "disposition", "severity", "true_positive", "urgency"}},
};

ModelSpec Bundled(Checkpoint c) {
  switch (c) {
    case Checkpoint::English: return {kBundleRepo, ""};
    case Checkpoint::Multilingual: return {kBundleRepo, "multilingual"};
    case Checkpoint::TypedDecisions: return {kBundleRepo, "typed-decisions"};
  }
  return {};
}
ModelSpec Standalone(Checkpoint c) {
  switch (c) {
    case Checkpoint::English: return {"convaiinnovations/laya", ""};
    case Checkpoint::Multilingual: return {"convaiinnovations/laya-multilingual", ""};
    case Checkpoint::TypedDecisions: return {"convaiinnovations/laya-typed-decisions", ""};
  }
  return {};
}

std::string UnknownModel(std::string_view name) {
  std::string out = "unknown model " + pyjson::Repr(name) + "; choose one of [";
  bool first = true;
  for (Checkpoint c : Checkpoints()) {
    if (!first) out += ", ";
    first = false;
    out += pyjson::Repr(Name(c));
  }
  out += "] (or an alias: [";
  first = true;
  for (const Alias& a : kAliases) {
    if (!first) out += ", ";
    first = false;
    out += pyjson::Repr(a.name);
  }
  return out + "])";
}

// s[:n] for a Python str: the first n code points.
std::string Head(const std::string& s, size_t n) {
  size_t i = 0;
  while (i < s.size() && n > 0) {
    unicodedata::Decoded d = unicodedata::DecodeRune(std::string_view(s).substr(i));
    i += d.width;
    n--;
  }
  return s.substr(0, i);
}

// os.path.expanduser on POSIX. A path it cannot expand is returned unchanged. Windows is not
// expanded (ntpath's rules differ and no lane exercises them yet).
std::string ExpandUser(const std::string& path) {
#if defined(_WIN32)
  return path;
#else
  if (path.empty() || path[0] != '~') return path;
  size_t end = path.find('/');
  if (end == std::string::npos) end = path.size();
  std::string home;
  std::vector<char> buf(16384);
  struct passwd pw;
  struct passwd* res = nullptr;
  if (end == 1) {
    const char* h = std::getenv("HOME");
    if (h != nullptr) {
      home = h;
    } else {
      if (getpwuid_r(getuid(), &pw, buf.data(), buf.size(), &res) != 0 || res == nullptr) return path;
      home = pw.pw_dir;
    }
  } else {
    std::string user = path.substr(1, end - 1);
    if (user.find('\0') != std::string::npos) return path;
    if (getpwnam_r(user.c_str(), &pw, buf.data(), buf.size(), &res) != 0 || res == nullptr) return path;
    home = pw.pw_dir;
  }
  while (!home.empty() && home.back() == '/') home.pop_back();
  std::string out = home + path.substr(end);
  return out.empty() ? "/" : out;
#endif
}

Checkpoint Pick(bool english) { return english ? Checkpoint::English : Checkpoint::Multilingual; }

}  // namespace

std::string_view Name(Checkpoint c) {
  switch (c) {
    case Checkpoint::English: return "english";
    case Checkpoint::Multilingual: return "multilingual";
    case Checkpoint::TypedDecisions: return "typed-decisions";
  }
  return "";
}

const std::array<Checkpoint, 3>& Checkpoints() {
  static const std::array<Checkpoint, 3> all = {Checkpoint::English, Checkpoint::Multilingual,
                                                 Checkpoint::TypedDecisions};
  return all;
}

std::string ModelSpec::String() const { return subfolder.empty() ? repo : repo + "/" + subfolder; }

std::string CanonicalName(std::string_view name) {
  std::string key = unicodedata::Lower(unicodedata::Strip(name));
  for (const Alias& a : kAliases)
    if (key == a.name) return std::string(Name(a.target));
  return key;
}

Result<Checkpoint> NormaliseName(std::string_view name) {
  std::string key = CanonicalName(name);
  for (Checkpoint c : Checkpoints())
    if (key == Name(c)) return Result<Checkpoint>::Ok(c);
  return Result<Checkpoint>::Fail(UnknownModel(name));
}

std::optional<ModelSpec> ResolveModelSpec(std::string_view name) {
  Result<Checkpoint> c = NormaliseName(name);
  if (!c.ok()) return std::nullopt;
  return Bundled(c.value());
}

std::string MatchTypedDecisionsWorkflow(const std::vector<std::string>& question_ids) {
  std::vector<std::string> ids = question_ids;
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  for (const Workflow& wf : kWorkflows) {
    if (ids.size() == wf.ids.size() && std::equal(ids.begin(), ids.end(), wf.ids.begin(),
                                                  [](const std::string& a, const char* b) { return a == b; }))
      return wf.name;
  }
  return "";
}

std::optional<bool> EnglishFromCode(std::string_view code_in) {
  std::string code = unicodedata::Lower(unicodedata::Strip(code_in));
  if (code.empty()) return std::nullopt;
  size_t dot = code.find('.');
  if (dot != std::string::npos) code.resize(dot);
  std::replace(code.begin(), code.end(), '_', '-');
  size_t dash = code.find('-');
  if (dash != std::string::npos) code.resize(dash);
  if (code.empty() || code == "c" || code == "posix" || code == "und" || code == "zxx" || code == "mul")
    return std::nullopt;
  return code == "en" || code == "eng" || code == "english";
}

LangHint LangCode(std::string code) {
  return [code = std::move(code)](const pyvalue::Value&) { return code; };
}

std::string Decision::Repr() const {
  return "RouteDecision(model=" + pyjson::Repr(Name(model)) + ", reason=" + pyjson::Repr(reason) + ")";
}

Options& Options::WithSource(std::string name, std::string repo) {
  sources.push_back({std::move(name), {std::move(repo), ""}, true});
  return *this;
}
Options& Options::WithSourceIn(std::string name, std::string repo, std::string subfolder) {
  sources.push_back({std::move(name), {std::move(repo), std::move(subfolder)}, false});
  return *this;
}

Router::Router() {
  for (Checkpoint c : Checkpoints()) models_[size_t(c)] = Bundled(c);
}

Result<Router> Router::Create(const Options& o) {
  Router r;
  r.default_ = Checkpoint::Multilingual;
  for (Checkpoint c : Checkpoints()) r.models_[size_t(c)] = o.standalone_repos ? Standalone(c) : Bundled(c);
  r.auto_ = o.auto_task_detection;
  r.hint_ = o.lang_guess;
  for (const Options::Source& s : o.sources) {
    Result<Checkpoint> key = NormaliseName(s.name);
    if (!key.ok()) return Result<Router>::Fail(key.error());
    ModelSpec spec = s.spec;
    if (s.expand_user) spec.repo = ExpandUser(spec.repo);
    r.models_[size_t(key.value())] = std::move(spec);
  }
  Result<Checkpoint> def = NormaliseName(o.default_model);
  if (!def.ok()) return Result<Router>::Fail(def.error());
  r.default_ = def.value();
  return Result<Router>::Ok(std::move(r));
}

Decision Router::Make(Checkpoint key, std::string reason, std::optional<lang::Analysis> det,
                      std::string wf) const {
  Decision d;
  d.model = key;
  d.repo = models_[size_t(key)].String();
  d.reason = std::move(reason);
  d.detection = std::move(det);
  d.workflow = std::move(wf);
  return d;
}

Result<Decision> Router::Route(const pyvalue::Value& state, const std::vector<std::string>& ids,
                               const CallOptions& c) const {
  using R = Result<Decision>;
  if (c.model) {
    Result<Checkpoint> key = NormaliseName(*c.model);
    if (!key.ok()) return R::Fail(key.error());
    return R::Ok(Make(key.value(), "explicit model=" + pyjson::Repr(*c.model), std::nullopt, ""));
  }
  if (c.task) {
    std::string name = unicodedata::Lower(*c.task);
    std::replace(name.begin(), name.end(), '-', '_');
    name = name == "typed_decisions" ? std::string(Name(Checkpoint::TypedDecisions)) : *c.task;
    Result<Checkpoint> key = NormaliseName(name);
    if (!key.ok()) return R::Fail(key.error());
    return R::Ok(Make(key.value(), "explicit task=" + pyjson::Repr(*c.task), std::nullopt, ""));
  }

  std::string wf = MatchTypedDecisionsWorkflow(ids);
  if (!wf.empty() && auto_)
    return R::Ok(Make(Checkpoint::TypedDecisions,
                      "question ids match the " + pyjson::Repr(wf) + " typed-decisions workflow", std::nullopt, wf));

  if (c.lang) {
    if (std::optional<bool> english = EnglishFromCode(*c.lang))
      return R::Ok(Make(Pick(*english), "explicit lang=" + pyjson::Repr(*c.lang), std::nullopt, wf));
  }

  const struct {
    const char* source;
    const LangHint* hint;
  } hints[] = {{"lang_guess", &c.lang_guess}, {"Router(lang_guess=...)", &hint_}};
  for (const auto& h : hints) {
    if (!*h.hint) continue;
    if (std::optional<bool> english = EnglishFromCode((*h.hint)(state)))
      return R::Ok(Make(Pick(*english),
                        std::string(h.source) + ": the caller identified this as " +
                            (*english ? "English" : "non-English") + " text",
                        std::nullopt, wf));
  }

  lang::Analysis det = lang::Analyze(state);
  Checkpoint key = default_;
  std::string reason;
  if (det.script == "unknown") {
    reason = "no letters detected in state; using default (" + std::string(Name(key)) + ")";
  } else if (det.script != "latin") {
    key = Checkpoint::Multilingual;
    reason = "non-Latin script (" + det.script + ", " + pyjson::Percent0(det.non_latin_fraction) +
             "% of letters); the English checkpoint cannot read it";
  } else if (!det.english) {
    key = Checkpoint::Multilingual;
    if (!det.mixed_segment.empty()) {
      reason = "Latin script, mostly English, but a line or field reads as " + pyjson::Repr(det.language) + " (" +
               pyjson::Repr(Head(det.mixed_segment, 60)) + "); the English checkpoint cannot read it";
    } else if (!det.language.empty()) {
      reason = "Latin script but language looks like " + pyjson::Repr(det.language) + ", not English";
    } else {
      reason = "Latin script, language not identified but " + pyjson::Percent0(det.diacritic_rate) +
               "% non-English letters; not safe for the English checkpoint";
    }
  } else if (det.language.empty()) {
    reason = "Latin script, language not identified and no non-English letters; using default (" +
             std::string(Name(key)) + ")";
  } else {
    key = Checkpoint::English;
    reason = "English Latin text";
  }
  return R::Ok(Make(key, std::move(reason), std::move(det), std::move(wf)));
}

Result<std::vector<Decision>> Router::RouteBatch(const std::vector<Request>& requests) const {
  using R = Result<std::vector<Decision>>;
  std::vector<Decision> out;
  out.reserve(requests.size());
  for (size_t i = 0; i < requests.size(); i++) {
    const Request& req = requests[i];
    if (req.state.is_null())
      return R::Fail("request " + std::to_string(i) + " 'state' must not be None; pass a string, dict, or list");
    Result<Decision> d = Route(req.state, req.question_ids, req.options);
    if (!d.ok()) return R::Fail(d.error());
    out.push_back(std::move(d).value());
  }
  return R::Ok(std::move(out));
}

}  // namespace router
}  // namespace laya
