//===-- ABISysV_mos.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABISysV_mos.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"
#include "lldb/ValueObject/ValueObjectMemory.h"
#include "lldb/ValueObject/ValueObjectRegister.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/TargetParser/Triple.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABISysV_mos, ABIMOS)

// MOS 6502 register numbers - matches MAME's gdb_register_map_m6502
enum dwarf_regnums {
  dwarf_a = 0,   // Accumulator
  dwarf_x,       // X index register
  dwarf_y,       // Y index register
  dwarf_p,       // Processor status (flags)
  dwarf_sp,      // Stack pointer (8-bit, implicitly in page 1)
  dwarf_pc,      // Program counter (16-bit)
};

// Register info for MOS 6502
// Order: A, X, Y, P, SP, PC (matches MAME format)
static const RegisterInfo g_register_infos[] = {
    {"a",
     nullptr,
     1,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_a, dwarf_a, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"x",
     nullptr,
     1,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_x, dwarf_x, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"y",
     nullptr,
     1,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_y, dwarf_y, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"p",
     "flags",
     1,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_p, dwarf_p, LLDB_REGNUM_GENERIC_FLAGS, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"sp",
     nullptr,
     1,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_sp, dwarf_sp, LLDB_REGNUM_GENERIC_SP, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"pc",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_pc, dwarf_pc, LLDB_REGNUM_GENERIC_PC, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    }};

static const uint32_t k_num_register_infos =
    sizeof(g_register_infos) / sizeof(RegisterInfo);

const lldb_private::RegisterInfo *
ABISysV_mos::GetRegisterInfoArray(uint32_t &count) {
  count = k_num_register_infos;
  return g_register_infos;
}

size_t ABISysV_mos::GetRedZoneSize() const { return 0; }

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------

ABISP
ABISysV_mos::CreateInstance(lldb::ProcessSP process_sp,
                            const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::mos) {
    return ABISP(
        new ABISysV_mos(std::move(process_sp), MakeMCRegisterInfo(arch)));
  }
  return ABISP();
}

bool ABISysV_mos::PrepareTrivialCall(Thread &thread, lldb::addr_t sp,
                                     lldb::addr_t pc, lldb::addr_t ra,
                                     llvm::ArrayRef<addr_t> args) const {
  // 6502 doesn't support traditional function calls via debugger
  return false;
}

bool ABISysV_mos::GetArgumentValues(Thread &thread,
                                    ValueList &values) const {
  return false;
}

Status ABISysV_mos::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                                         lldb::ValueObjectSP &new_value_sp) {
  return Status();
}

ValueObjectSP ABISysV_mos::GetReturnValueObjectSimple(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  return return_valobj_sp;
}

ValueObjectSP ABISysV_mos::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  return return_valobj_sp;
}

// Called when we are on the first instruction of a new function
// For 6502, the return address was pushed to stack by JSR
UnwindPlanSP ABISysV_mos::CreateFunctionEntryUnwindPlan() {
  uint32_t sp_reg_num = dwarf_sp;
  uint32_t pc_reg_num = dwarf_pc;

  UnwindPlan::Row row;
  // CFA is SP + 2 (after JSR pushes 2-byte return address)
  // But 6502 SP is only 8-bit and points within page 1
  row.GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 2);
  row.SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, -2, true);
  row.SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("mos 6502 at-func-entry default");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  return plan_sp;
}

UnwindPlanSP ABISysV_mos::CreateDefaultUnwindPlan() {
  uint32_t sp_reg_num = dwarf_sp;
  uint32_t pc_reg_num = dwarf_pc;

  UnwindPlan::Row row;
  row.GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 2);
  row.SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, -2, true);
  row.SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindDWARF);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("mos 6502 default unwind plan");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return plan_sp;
}

bool ABISysV_mos::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

bool ABISysV_mos::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  // On 6502, no registers are automatically callee-saved
  // The caller must save any registers it needs preserved
  return false;
}

void ABISysV_mos::Initialize(void) {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "System V ABI for MOS 6502 targets", CreateInstance);
}

void ABISysV_mos::Terminate(void) {
  PluginManager::UnregisterPlugin(CreateInstance);
}
