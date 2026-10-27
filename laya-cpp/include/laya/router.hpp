#pragma once
// Pure routing, a port of the routing half of laya/router.py (via laya-go/router): which of the
// three shipped checkpoints should read a request, and why. Nothing is loaded and no model runs.
//
// Not ported here: QuestionSchema (needs the C3 schema types; lands with C10), on_route hooks
// (C8b), loading, leases and routed predict (C10). Python's `explain=` diagnostics are not ported.
//
// Errors: every failure is a Result<T> with Python's message text. Two cases exist: a model, task,
// default or source name that is neither a checkpoint nor an alias (the text lists the sorted names
// and aliases), and a RouteBatch request whose state is null.
//
// Thread safety: a Router is immutable once Create returns, and Route/RouteBatch are const, so one
// Router may be used from any number of threads. The LangHint callables (Options::lang_guess and
// CallOptions::lang_guess) are called on whichever thread routes, so they must be safe for
// concurrent use. Strings are UTF-8 and must be valid; a name or code with invalid bytes is read as
// U+FFFD would be, not rejected.
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "laya/lang.hpp"
#include "laya/pyvalue.hpp"
#include "laya/result.hpp"

namespace laya {
namespace router {

// The shipped checkpoints. A closed set: a name that is not one of these is an error, never a
// fourth value.
enum class Checkpoint { English, Multilingual, TypedDecisions };

// The checkpoint's name as Python spells it: "english", "multilingual", "typed-decisions".
std::string_view Name(Checkpoint c);
// The three checkpoints in sorted name order.
const std::array<Checkpoint, 3>& Checkpoints();

constexpr const char* kBundleRepo = "convaiinnovations/laya";

// Where a checkpoint comes from: a repo id or a local path, and an optional subfolder of it.
struct ModelSpec {
  std::string repo;
  std::string subfolder;
  std::string String() const;  // laya's _repo_str: "repo", or "repo/subfolder"
  bool operator==(const ModelSpec& o) const { return repo == o.repo && subfolder == o.subfolder; }
};

// ---- names ---------------------------------------------------------------------------------

// str(name).strip().lower() with an alias resolved; does not ask whether the result is a
// checkpoint: laya's canonical_name.
std::string CanonicalName(std::string_view name);
// The checkpoint a name or alias stands for, or Python's "unknown model ..." text.
Result<Checkpoint> NormaliseName(std::string_view name);
// The shipped source of a checkpoint name or alias; nullopt when the name is neither: laya's
// resolve_model_spec.
std::optional<ModelSpec> ResolveModelSpec(std::string_view name);
// The typed-decisions workflow whose question ids these are, or "". The id set must match
// exactly (order and repeats do not matter), so an unrelated schema that holds "urgency" is never
// captured.
std::string MatchTypedDecisionsWorkflow(const std::vector<std::string>& question_ids);
// true for English, false for another language, nullopt when the code identifies nothing: blank,
// "C", "POSIX", "C.UTF-8", "und", "zxx", "mul". Reads "en", "EN", "en-US", "en_US", "en_US.UTF-8".
std::optional<bool> EnglishFromCode(std::string_view code);

// ---- routing -------------------------------------------------------------------------------

// Answers, for a state, which language it is in: a code such as "en", "de-DE" or "en_US.UTF-8".
// Returning "" or a code that names no language ("C", "und") abstains, which sends the request on
// to detection. Must be safe for concurrent use.
using LangHint = std::function<std::string(const pyvalue::Value& state)>;
// A hint that always answers code.
LangHint LangCode(std::string code);

// The routing outcome.
struct Decision {
  Checkpoint model = Checkpoint::Multilingual;
  std::string repo;    // "repo" or "repo/subfolder", where the Router would load it from
  std::string reason;  // why, in laya's words: part of the contract
  // The language analysis the decision rests on; nullopt when an explicit model, task, lang, hint
  // or workflow decided before detection ran.
  std::optional<lang::Analysis> detection;
  std::string workflow;  // the typed-decisions workflow the ids match, or ""
  // laya's repr of a RouteDecision: RouteDecision(model='english', reason='...').
  std::string Repr() const;
};

struct Options {
  std::string default_model = "multilingual";  // for text whose language cannot be identified
  bool auto_task_detection = false;            // route an exact workflow id match to typed-decisions
  bool standalone_repos = false;               // per-checkpoint hub repos instead of bundle subfolders
  LangHint lang_guess;                         // a hint for every request; empty for none
  struct Source {
    std::string name;
    ModelSpec spec;
    bool expand_user;  // a leading "~" is expanded as os.path.expanduser does
  };
  std::vector<Source> sources;  // applied in order, the last one for a name winning
  // Points a built-in checkpoint (name or alias) at a repo id or local path; "~" is expanded.
  Options& WithSource(std::string name, std::string repo);
  // The same for a repo and a subfolder of it; the repo is not expanded.
  Options& WithSourceIn(std::string name, std::string repo, std::string subfolder);
};

// Overrides for one request. Precedence: model > task > workflow (when auto_task_detection) > lang
// > lang_guess here > Options::lang_guess > detection > default.
struct CallOptions {
  std::optional<std::string> model;  // a checkpoint name or alias
  std::optional<std::string> task;   // "typed_decisions"/"typed-decisions" in any case, or a checkpoint
  std::optional<std::string> lang;   // decisive only when the code names a language
  LangHint lang_guess;               // checked after lang, before the Router's own hint
};

struct Request {
  pyvalue::Value state;                    // must not be null in RouteBatch
  std::vector<std::string> question_ids;   // only the ids are read, to recognise a workflow
  CallOptions options;
};

class Router {
 public:
  // Builds a Router; fails with the unknown-model text when default_model or a source name is
  // neither a checkpoint nor an alias.
  static Result<Router> Create(const Options& options = {});
  Router();  // the same as Create() with default Options; here for Result<Router>

  Checkpoint Default() const { return default_; }
  const ModelSpec& Spec(Checkpoint c) const { return models_[size_t(c)]; }

  // Decides which checkpoint reads state, without loading or running anything.
  Result<Decision> Route(const pyvalue::Value& state, const std::vector<std::string>& question_ids = {},
                         const CallOptions& options = {}) const;
  // Routes a batch; decisions keep the order of requests. A null state is refused with laya's text
  // and the first failing request ends the batch. Python's route_batch takes no routing options
  // of its own (only hook options, C8b), so neither does this.
  Result<std::vector<Decision>> RouteBatch(const std::vector<Request>& requests) const;

 private:
  Decision Make(Checkpoint key, std::string reason, std::optional<lang::Analysis> det, std::string wf) const;
  std::array<ModelSpec, 3> models_;
  Checkpoint default_ = Checkpoint::Multilingual;
  bool auto_ = false;
  LangHint hint_;
};

}  // namespace router
}  // namespace laya
