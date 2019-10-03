#ifndef EMP_SIGNALGP_V2_EXECSTEPPER_H
#define EMP_SIGNALGP_V2_EXECSTEPPER_H

#include <iostream>
#include <utility>

#include "base/Ptr.h"
#include "base/vector.h"
#include "tools/Random.h"
#include "tools/MatchBin.h"
#include "tools/matchbin_utils.h"

#include "../EventLibrary.h"
#include "../InstructionLibrary.h"

#include "SignalGP.h"
#include "LinearProgram.h"

namespace emp { namespace sgp_v2 {

  // Each program type needs their own 'ExecutionStepper' to manage execution
  // - knows about program structure
  // - knows how to make programs
  // - knows how to execute programs
  // TODO - turn everything into configurable lambdas?
  template<typename MEMORY_MODEL_T,
           typename TAG_T=emp::BitSet<16>,
           typename INST_ARGUMENT_T=int,
           typename MATCHBIN_T=emp::MatchBin< size_t, emp::HammingMetric<16>, emp::RankedSelector<std::ratio<16+8, 16> >>
           >
  class SimpleExecutionStepper {
  public:
    struct ExecState;
    struct Module;

    enum class InstProperty { MODULE };

    using exec_stepper_t = SimpleExecutionStepper<MEMORY_MODEL_T, TAG_T, INST_ARGUMENT_T, MATCHBIN_T>;
    using exec_state_t = ExecState;

    using tag_t = TAG_T;
    using arg_t = INST_ARGUMENT_T;
    using module_t = Module;
    using matchbin_t = MATCHBIN_T;

    using memory_model_t = MEMORY_MODEL_T;
    using memory_state_t = typename memory_model_t::memory_state_t;

    using program_t = SimpleProgram<tag_t, arg_t>;

    using hardware_t = SignalGP<exec_stepper_t>;
    using thread_t = typename hardware_t::thread_t;

    using inst_t = typename program_t::inst_t;
    using inst_prop_t = InstProperty;
    using inst_lib_t = InstructionLibrary<hardware_t, inst_t, inst_prop_t>;

    /// Library of flow types.
    /// e.g., WHILE, IF, ROUTINE, et cetera
    /// NOTE - I'm not sure that I'm a fan of how this is organized/named/setup.
    enum class FlowType : size_t { BASIC, WHILE_LOOP, ROUTINE, CALL };
    struct FlowHandler {
      struct FlowControl {
        using flow_control_fun_t = std::function<void(exec_state_t &)>;
        flow_control_fun_t open_flow_fun;
        flow_control_fun_t close_flow_fun;
        flow_control_fun_t break_flow_fun;
      };
      std::map<FlowType, FlowControl> lib = { {FlowType::BASIC, FlowControl()},
                                              {FlowType::WHILE_LOOP, FlowControl()},
                                              {FlowType::ROUTINE, FlowControl()},
                                              {FlowType::CALL, FlowControl()} };

      FlowControl & operator[](FlowType type) {
        emp_assert(Has(lib, type), "FlowType not recognized!");
        return lib[type];
      }

      const FlowControl & operator[](FlowType type) const {
        emp_assert(Has(lib, type), "FlowType not recognized!");
        return lib[type];
      }

      std::string FlowTypeToString(FlowType type) const {
        switch (type) {
          case FlowType::BASIC: return "BASIC";
          case FlowType::WHILE_LOOP: return "WHILE_LOOP";
          case FlowType::ROUTINE: return "ROUTINE";
          case FlowType::CALL: return "CALL";
          default: return "UNKNOWN";
        }
      }
    };

    /// Base flow information
    struct FlowInfo {
      FlowType type;        ///< Flow type ID?
      size_t mp;        ///< Module pointer. Which module is being executed?
      size_t ip;        ///< Instruction pointer. Which instruction is executed?

      size_t begin;     ///< Where does the flow begin?
      size_t end;       ///< Where does the flow end?

      FlowInfo(FlowType _type, size_t _mp=(size_t)-1, size_t _ip=(size_t)-1,
               size_t _begin=(size_t)-1, size_t _end=(size_t)-1)
        : type(_type), mp(_mp), ip(_ip), begin(_begin), end(_end) { ; }
    };

    struct CallState {
      // Local memory
      // memory!
      // flow stack
      memory_state_t memory;
      emp::vector<FlowInfo> flow_stack; ///< Stack of 'Flow' (read heads)

      CallState(const memory_state_t & _mem=memory_state_t())
        : memory(_mem), flow_stack() { ; }
    };

    /// Execution State.
    struct ExecState {
      // CallStack
      emp::vector<CallState> call_stack;   ///< Program call stack.

      void Clear() {
        call_stack.clear();
      }
    };

    /// Module definition.
    struct Module {
      size_t id;      ///< Module ID. Used to call/reference module.
      size_t begin;   ///< First instruction in module (will be the module definition instruction).
      size_t end;     ///< The last instruction in the module.
      tag_t tag;      ///< Module tag. Used to call/reference module.
      std::unordered_set<size_t> in_module; ///< instruction positions belonging to this module.
      // todo

      Module(size_t _id, size_t _begin=0, size_t _end=0, const tag_t & _tag=tag_t())
        : id(_id), begin(_begin), end(_end), tag(_tag), in_module() { ; }

      size_t GetSize() const { return in_module.size(); }

      size_t GetID() const { return id; }

      tag_t & GetTag() { return tag; }
      const tag_t & GetTag() const { return tag; }

      bool InModule(size_t ip) const { return Has(in_module, ip); }
    };

  protected:
    Ptr<inst_lib_t> inst_lib;
    FlowHandler flow_handler;

    memory_model_t memory_model;

    program_t program;

    emp::vector<module_t> modules;

    tag_t default_module_tag;

    Ptr<Random> random_ptr;         // TODO - does signalgp need a random number generator anymore?

    matchbin_t matchbin;
    bool is_matchbin_cache_dirty;
    std::function<void()> fun_clear_matchbin_cache = [this](){ this->ResetMatchBin(); };

    void SetupDefaultFlowControl() { // TODO!
      flow_handler[FlowType::BASIC].open_flow_fun = [](exec_state_t & exec_state) { ; };
      flow_handler[FlowType::BASIC].close_flow_fun = [](exec_state_t & exec_state) { ; };
      flow_handler[FlowType::BASIC].break_flow_fun = [](exec_state_t & exec_state) { ; };
      flow_handler[FlowType::WHILE_LOOP].open_flow_fun = [](exec_state_t & exec_state) { ; };
      flow_handler[FlowType::WHILE_LOOP].close_flow_fun = [](exec_state_t & exec_state) { ; };
      flow_handler[FlowType::WHILE_LOOP].break_flow_fun = [](exec_state_t & exec_state) { ; };
      flow_handler[FlowType::ROUTINE].open_flow_fun = [](exec_state_t & exec_state) { ; };
      flow_handler[FlowType::ROUTINE].close_flow_fun = [](exec_state_t & exec_state) { ; };
      flow_handler[FlowType::ROUTINE].break_flow_fun = [](exec_state_t & exec_state) { ; };
      flow_handler[FlowType::CALL].open_flow_fun = [](exec_state_t & exec_state) { ; };
      flow_handler[FlowType::CALL].close_flow_fun = [](exec_state_t & exec_state) { ; };
      flow_handler[FlowType::CALL].break_flow_fun = [](exec_state_t & exec_state) { ; };
    }

  public:
    SimpleExecutionStepper(Ptr<inst_lib_t> ilib,
                           Ptr<Random> rnd)
      : inst_lib(ilib),
        flow_handler(),
        memory_model(),
        program(),
        modules(),
        default_module_tag(),
        random_ptr(rnd),
        matchbin(rnd ? *rnd : *emp::NewPtr<emp::Random>()),
        is_matchbin_cache_dirty(true)
    {
      // Configure default flow control
      SetupDefaultFlowControl();
    }

    void ResetMatchBin() {
      // todo!
      matchbin.Clear();
      is_matchbin_cache_dirty = false;
      // TODO - reset match bin!
      for (size_t i = 0; i < modules.size(); ++i) {
        matchbin.Set(i, modules[i].GetTag(), i);
      }
    }

    void SingleExecutionStep(hardware_t & hardware, exec_state_t & exec_state) {
      std::cout << "SingleExecutionStep!" << std::endl;
    }

    void InitThread(thread_t & thread, size_t module_id) {
      emp_assert(module_id < modules.size(), "Invalid module ID.");
      // Initialize new thread!
      std::cout << "InitThread!" << std::endl;
      exec_state_t & state = thread.GetExecState();
      if (state.call_stack.size()) { state.Clear(); }
      // (1) create fresh memory state
      state.call_stack.emplace_back(memory_model.CreateMemoryState());
      // (2) Set exec state up
      CallState & call_state = state.call_stack.back();
      // flow type, ip, mp, begin, end
      module_t & module_info = modules[module_id];
      call_state.flow_stack.emplace_back(FlowType::CALL, module_info.begin, module_id, module_info.begin, module_info.end);
    }

    // Find best module given a tag.
    emp::vector<size_t> FindModuleMatch(const tag_t & tag, size_t n=1) {
      // Find n matches.
      if(is_matchbin_cache_dirty){
        ResetMatchBin();
      }
      // no need to transform to values because we're using
      // matchbin uids equivalent to function uids
      return matchbin.Match(tag, n);
    }

    // Load program!
    // - program = in_program
    // - initialize program modules! ('compilation')
    /// Set program for this hardware object.
    /// After updating hardware's program, run 'UpdateModules'.
    void SetProgram(const program_t & _program) {
      program = _program;
      UpdateModules();
    }

    void SetDefaultTag(const tag_t & _tag) {
      default_module_tag = _tag;
    }

    // todo - check to see if this works
    void UpdateModules() {
      std::cout << "Update modules!" << std::endl;
      // Clear out the current modules.
      modules.clear();
      // Do nothing if there aren't any instructions to look at.
      if (!program.GetSize()) return;
      // Scan program for module definitions.
      std::unordered_set<size_t> dangling_instructions;
      for (size_t pos = 0; pos < program.GetSize(); ++pos) {
        inst_t & inst = program[pos];
        // Is this a module definition?
        if (inst_lib->HasProperty(inst.GetID(), inst_prop_t::MODULE)) {
          // If this isn't the first module we've found, mark this position as the
          // last position of the previous module.
          if (modules.size()) { modules.back().end = pos; }
          emp_assert(inst.GetTags().size(), "MODULE-defining instructions must have tag arguments to be used with this execution stepper.");
          const size_t mod_id = modules.size(); // Module ID for new module.
          modules.emplace_back(mod_id, ( (pos+1) < program.GetSize() ) ? pos+1 : 0, -1, inst.GetTags()[0]);
        } else {
          // We didn't find a new module. Track which module this instruction belongs to:
          // - If we've found a module, add it to the current module.
          // - If we haven't found a module, note that this instruction is dangling.
          if (modules.size()) { modules.back().in_module.emplace(pos); }
          else { dangling_instructions.emplace(pos); }
        }
      }
      // At this point, we know about all of the modules (if any).
      // First, we need to set the end point for the last module we found.
      if (modules.size()) {
        // If the first module begins at the beginning of the instruction, the last
        // module must end at the end of the program.
        // Otherwise, the last module ends where the first module begins.
        // if (modules[0].begin == 0) modules.back().end = program.GetSize();
        // else
        modules.back().end = (modules[0].begin - 1 > 0) ? modules[0].begin - 1 : program.GetSize();
      } else {
        // Found no modules. Add a default module that starts at the beginning and
        // ends at the end.
        modules.emplace_back(0, 0, program.GetSize(), default_module_tag);
      }
      // Now, we need to take care of the dangling instructions.
      // - We're going to assume the program is circular, so dangling instructions
      //   belong to the last module we found.
      for (size_t val : dangling_instructions) modules.back().in_module.emplace(val);

      // Reset matchbin
      ResetMatchBin();
    }

    emp::vector<module_t> & GetModules() { return modules;  }
    size_t GetNumModules() const { return modules.size(); }

    program_t & GetProgram() { return program; }

    memory_model_t & GetMemoryModel() { return memory_model; }

    void PrintModules(std::ostream & os=std::cout) const {
      os << "Modules: [";
      for (size_t i = 0; i < modules.size(); ++i) {
        if (i) os << ",";
        os << "{id:" << modules[i].id << ", begin:" << modules[i].begin << ", end:" << modules[i].end << ", tag:" << modules[i].tag << "}";
      }
      os << "]";
    }

    void PrintExecutionState(const exec_state_t & state, std::ostream & os=std::cout) const {
      // -- Call stack --
      // todo
      os << "Call stack (" << state.call_stack.size() << "):\n";
      os << "------ TOP ------\n";
      for (auto it = state.call_stack.rbegin(); it != state.call_stack.rend(); ++it) {
        const CallState & call_state = *it;
        if (call_state.flow_stack.size()) {
          const FlowInfo & top_flow = call_state.flow_stack.back();
          // MP, IP, ...
          // type, mp, ip, begin, end
          // todo - print full flow stack!
          os << "Call: {mp:" << top_flow.mp
             << ", ip:" << top_flow.ip
             << ", flow-begin:" << top_flow.begin
             << ", flow-end:" << top_flow.end
             << ", flow-type:" << flow_handler.FlowTypeToString(top_flow.type)
             << "}\n";
        }
        memory_model.PrintMemoryState(call_state.memory, os);
        os << "---\n";
      }
      os << "-----------------";
    }

  };

}}

#endif