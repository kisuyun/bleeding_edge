// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_IA32.
#if defined(TARGET_ARCH_IA32)

#include "vm/intermediate_language.h"

#include "lib/error.h"
#include "vm/flow_graph_compiler.h"
#include "vm/locations.h"
#include "vm/object_store.h"
#include "vm/parser.h"
#include "vm/stub_code.h"
#include "vm/symbols.h"

#define __ compiler->assembler()->

namespace dart {

DECLARE_FLAG(int, optimization_counter_threshold);
DECLARE_FLAG(bool, trace_functions);

// Generic summary for call instructions that have all arguments pushed
// on the stack and return the result in a fixed register EAX.
LocationSummary* Computation::MakeCallSummary() {
  LocationSummary* result = new LocationSummary(0, 0, LocationSummary::kCall);
  result->set_out(Location::RegisterLocation(EAX));
  return result;
}


void BindInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  computation()->EmitNativeCode(compiler);
  if (is_used() && locs()->out().IsRegister()) {
    // TODO(vegorov): this should really happen only for comparisons fused
    // with branches.  Currrently IR does not provide an easy way to remove
    // instructions from the graph so we just leave fused comparison in it
    // but change its result location to be NoLocation.
    compiler->frame_register_allocator()->Push(locs()->out().reg(), this);
  }
}


LocationSummary* ReturnInstr::MakeLocationSummary() const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 1;
  LocationSummary* locs =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kNoCall);
  locs->set_in(0, Location::RegisterLocation(EAX));
  locs->set_temp(0, Location::RequiresRegister());
  return locs;
}


void ReturnInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register result = locs()->in(0).reg();
  Register temp = locs()->temp(0).reg();
  ASSERT(result == EAX);
  if (!compiler->is_optimizing()) {
    // Count only in unoptimized code.
    // TODO(srdjan): Replace the counting code with a type feedback
    // collection and counting stub.
    const Function& function =
          Function::ZoneHandle(compiler->parsed_function().function().raw());
    __ LoadObject(temp, function);
    __ incl(FieldAddress(temp, Function::usage_counter_offset()));
    if (FlowGraphCompiler::CanOptimize()) {
      // Do not optimize if usage count must be reported.
      __ cmpl(FieldAddress(temp, Function::usage_counter_offset()),
          Immediate(FLAG_optimization_counter_threshold));
      Label not_yet_hot, already_optimized;
      __ j(LESS, &not_yet_hot, Assembler::kNearJump);
      __ j(GREATER, &already_optimized, Assembler::kNearJump);
      __ pushl(result);  // Preserve result.
      __ pushl(temp);  // Argument for runtime: function to optimize.
      __ CallRuntime(kOptimizeInvokedFunctionRuntimeEntry);
      __ popl(temp);  // Remove argument.
      __ popl(result);  // Restore result.
      __ Bind(&not_yet_hot);
      __ Bind(&already_optimized);
    }
  }
  if (FLAG_trace_functions) {
    const Function& function =
        Function::ZoneHandle(compiler->parsed_function().function().raw());
    __ LoadObject(temp, function);
    __ pushl(result);  // Preserve result.
    __ pushl(temp);
    compiler->GenerateCallRuntime(AstNode::kNoId,
                                  0,
                                  CatchClauseNode::kInvalidTryIndex,
                                  kTraceFunctionExitRuntimeEntry);
    __ popl(temp);  // Remove argument.
    __ popl(result);  // Restore result.
  }
  __ LeaveFrame();
  __ ret();

  // Generate 1 byte NOP so that the debugger can patch the
  // return pattern with a call to the debug stub.
  __ nop(1);
  compiler->AddCurrentDescriptor(PcDescriptors::kReturn,
                                 cid(),
                                 token_pos(),
                                 CatchClauseNode::kInvalidTryIndex);
}


LocationSummary* ClosureCallComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 0;
  const intptr_t kNumTemps = 1;
  LocationSummary* result =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
  result->set_out(Location::RegisterLocation(EAX));
  result->set_temp(0, Location::RegisterLocation(EDX));  // Arg. descriptor.
  return result;
}


LocationSummary* LoadLocalComp::MakeLocationSummary() const {
  return LocationSummary::Make(0,
                               Location::RequiresRegister(),
                               LocationSummary::kNoCall);
}


void LoadLocalComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register result = locs()->out().reg();
  __ movl(result, Address(EBP, local().index() * kWordSize));
}


LocationSummary* StoreLocalComp::MakeLocationSummary() const {
  return LocationSummary::Make(1,
                               Location::SameAsFirstInput(),
                               LocationSummary::kNoCall);
}


void StoreLocalComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register value = locs()->in(0).reg();
  Register result = locs()->out().reg();
  ASSERT(result == value);  // Assert that register assignment is correct.
  __ movl(Address(EBP, local().index() * kWordSize), value);
}


LocationSummary* ConstantVal::MakeLocationSummary() const {
  return LocationSummary::Make(0,
                               Location::RequiresRegister(),
                               LocationSummary::kNoCall);
}


void ConstantVal::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register result = locs()->out().reg();
  __ LoadObject(result, value());
}


LocationSummary* AssertAssignableComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 3;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
  summary->set_in(0, Location::RegisterLocation(EAX));  // Value.
  summary->set_in(1, Location::RegisterLocation(ECX));  // Instantiator.
  summary->set_in(2, Location::RegisterLocation(EDX));  // Type arguments.
  summary->set_out(Location::RegisterLocation(EAX));
  return summary;
}


LocationSummary* AssertBooleanComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* locs =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
  locs->set_in(0, Location::RegisterLocation(EAX));
  locs->set_out(Location::RegisterLocation(EAX));
  return locs;
}


void AssertBooleanComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register obj = locs()->in(0).reg();
  Register result = locs()->out().reg();

  // Check that the type of the value is allowed in conditional context.
  // Call the runtime if the object is not bool::true or bool::false.
  Label done;
  __ CompareObject(obj, compiler->bool_true());
  __ j(EQUAL, &done, Assembler::kNearJump);
  __ CompareObject(obj, compiler->bool_false());
  __ j(EQUAL, &done, Assembler::kNearJump);

  __ pushl(obj);  // Push the source object.
  compiler->GenerateCallRuntime(cid(),
                                token_pos(),
                                try_index(),
                                kConditionTypeErrorRuntimeEntry);
  // We should never return here.
  __ int3();

  __ Bind(&done);
  ASSERT(obj == result);
}


static Condition TokenKindToSmiCondition(Token::Kind kind) {
  switch (kind) {
    case Token::kEQ: return EQUAL;
    case Token::kNE: return NOT_EQUAL;
    case Token::kLT: return LESS;
    case Token::kGT: return GREATER;
    case Token::kLTE: return LESS_EQUAL;
    case Token::kGTE: return  GREATER_EQUAL;
    default:
      UNREACHABLE();
      return OVERFLOW;
  }
}


LocationSummary* EqualityCompareComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 2;
  if (receiver_class_id() != kObject) {
    ASSERT((receiver_class_id() == kSmi) || (receiver_class_id() == kDouble));
    const intptr_t kNumTemps = 1;
    LocationSummary* locs =
        new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kNoCall);
    locs->set_in(0, Location::RequiresRegister());
    locs->set_in(1, Location::RequiresRegister());
    locs->set_temp(0, Location::RequiresRegister());
    locs->set_out(Location::RequiresRegister());
    return locs;
  }
  if (HasICData() && (ic_data()->NumberOfChecks() > 0)) {
    const intptr_t kNumTemps = 1;
    LocationSummary* locs =
        new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
    locs->set_in(0, Location::RegisterLocation(ECX));
    locs->set_in(1, Location::RegisterLocation(EDX));
    locs->set_temp(0, Location::RegisterLocation(EBX));
    locs->set_out(Location::RegisterLocation(EAX));
    return locs;
  }
  const intptr_t kNumTemps = 0;
  LocationSummary* locs =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
  locs->set_in(0, Location::RegisterLocation(ECX));
  locs->set_in(1, Location::RegisterLocation(EDX));
  locs->set_out(Location::RegisterLocation(EAX));
  return locs;
}


// Optional integer arguments can often be null. Null is not collected
// by IC data. TODO(srdjan): Shall we collect null classes in ICData as well?
static void EmitSmiEqualityCompare(FlowGraphCompiler* compiler,
                                   EqualityCompareComp* comp) {
  Register left = comp->locs()->in(0).reg();
  Register right = comp->locs()->in(1).reg();
  Register temp = comp->locs()->temp(0).reg();
  Label* deopt = compiler->AddDeoptStub(comp->cid(),
                                        comp->token_pos(),
                                        comp->try_index(),
                                        kDeoptSmiCompareSmi,
                                        left,
                                        right);
  __ movl(temp, left);
  __ orl(temp, right);
  __ testl(temp, Immediate(kSmiTagMask));
  __ j(NOT_ZERO, deopt);
  __ cmpl(left, right);
  Register result = comp->locs()->out().reg();
  Label load_true, done;
  __ j(TokenKindToSmiCondition(comp->kind()), &load_true, Assembler::kNearJump);
  __ LoadObject(result, compiler->bool_false());
  __ jmp(&done, Assembler::kNearJump);
  __ Bind(&load_true);
  __ LoadObject(result, compiler->bool_true());
  __ Bind(&done);
}


// TODO(srdjan): Add support for mixed Smi/Double equality
// (see LoadDoubleOrSmiToXmm).
static void EmitDoubleEqualityCompare(FlowGraphCompiler* compiler,
                                      EqualityCompareComp* comp) {
  Register left = comp->locs()->in(0).reg();
  Register right = comp->locs()->in(1).reg();
  Register temp = comp->locs()->temp(0).reg();
  Label* deopt = compiler->AddDeoptStub(comp->cid(),
                                        comp->token_pos(),
                                        comp->try_index(),
                                        kDeoptDoubleCompareDouble,
                                        left,
                                        right);
  Label done, is_false, is_true;
  __ CompareClassId(left, kDouble, temp);
  __ j(NOT_EQUAL, deopt);
  __ CompareClassId(right, kDouble, temp);
  __ j(NOT_EQUAL, deopt);
  __ movsd(XMM0, FieldAddress(left, Double::value_offset()));
  __ movsd(XMM1, FieldAddress(right, Double::value_offset()));
  compiler->EmitDoubleCompareBool(TokenKindToSmiCondition(comp->kind()),
                                  XMM0, XMM1,
                                  comp->locs()->out().reg());
}


static void EmitEqualityAsInstanceCall(FlowGraphCompiler* compiler,
                                       EqualityCompareComp* comp) {
  compiler->AddCurrentDescriptor(PcDescriptors::kDeopt,
                                 comp->cid(),
                                 comp->token_pos(),
                                 comp->try_index());
  const String& operator_name = String::ZoneHandle(Symbols::New("=="));
  const int kNumberOfArguments = 2;
  const Array& kNoArgumentNames = Array::Handle();
  const int kNumArgumentsChecked = 2;

  compiler->GenerateInstanceCall(comp->cid(),
                                 comp->token_pos(),
                                 comp->try_index(),
                                 operator_name,
                                 kNumberOfArguments,
                                 kNoArgumentNames,
                                 kNumArgumentsChecked);
  ASSERT(comp->locs()->out().reg() == EAX);
  if (comp->kind() == Token::kNE) {
    Label done, false_label;
    __ CompareObject(EAX, compiler->bool_true());
    __ j(EQUAL, &false_label, Assembler::kNearJump);
    __ LoadObject(EAX, compiler->bool_true());
    __ jmp(&done, Assembler::kNearJump);
    __ Bind(&false_label);
    __ LoadObject(EAX, compiler->bool_false());
    __ Bind(&done);
  }
}


static void EmitEqualityAsPolymorphicCall(FlowGraphCompiler* compiler,
                                          const ICData& orig_ic_data,
                                          const LocationSummary& locs,
                                          BranchInstr* branch,
                                          Token::Kind kind,
                                          intptr_t cid,
                                          intptr_t token_pos,
                                          intptr_t try_index) {
  ASSERT((kind == Token::kEQ) || (kind == Token::kNE));
  const ICData& ic_data = ICData::Handle(orig_ic_data.AsUnaryClassChecks());
  ASSERT(ic_data.NumberOfChecks() > 0);
  ASSERT(ic_data.num_args_tested() == 1);
  Label* deopt = compiler->AddDeoptStub(cid,
                                        token_pos,
                                        try_index,
                                        kDeoptEquality);
  Register left = locs.in(0).reg();
  Register right = locs.in(1).reg();
  __ testl(left, Immediate(kSmiTagMask));
  Register temp = locs.temp(0).reg();
  if (ic_data.GetReceiverClassIdAt(0) == kSmi) {
    Label done, load_class_id;
    __ j(NOT_ZERO, &load_class_id, Assembler::kNearJump);
    __ movl(temp, Immediate(kSmi));
    __ jmp(&done, Assembler::kNearJump);
    __ Bind(&load_class_id);
    __ LoadClassId(temp, left);
    __ Bind(&done);
  } else {
    __ j(ZERO, deopt);  // Smi deopts.
    __ LoadClassId(temp, left);
  }
  Condition cond = TokenKindToSmiCondition(kind);
  Label done;
  for (intptr_t i = 0; i < ic_data.NumberOfChecks(); i++) {
    ASSERT((ic_data.GetReceiverClassIdAt(i) != kSmi) || (i == 0));
    Label next_test;
    __ cmpl(temp, Immediate(ic_data.GetReceiverClassIdAt(i)));
    __ j(NOT_EQUAL, &next_test);
    const Function& target = Function::ZoneHandle(ic_data.GetTargetAt(i));
    ObjectStore* object_store = Isolate::Current()->object_store();
    if (target.owner() == object_store->object_class()) {
      // Object.== is same as ===.
      __ Drop(2);
      __ cmpl(left, right);
      if (branch != NULL) {
        branch->EmitBranchOnCondition(compiler, cond);
      } else {
        // This case should be rare.
        Register result = locs.out().reg();
        Label load_true;
        __ j(cond, &load_true, Assembler::kNearJump);
        __ LoadObject(result, compiler->bool_false());
        __ jmp(&done);
        __ Bind(&load_true);
        __ LoadObject(result, compiler->bool_true());
      }
    } else {
      const int kNumberOfArguments = 2;
      const Array& kNoArgumentNames = Array::Handle();
      compiler->GenerateStaticCall(cid,
                                   token_pos,
                                   try_index,
                                   target,
                                   kNumberOfArguments,
                                   kNoArgumentNames);
      if (branch == NULL) {
        if (kind == Token::kNE) {
          Label false_label;
          __ CompareObject(EAX, compiler->bool_true());
          __ j(EQUAL, &false_label, Assembler::kNearJump);
          __ LoadObject(EAX, compiler->bool_true());
          __ jmp(&done);
          __ Bind(&false_label);
          __ LoadObject(EAX, compiler->bool_false());
          __ jmp(&done);
        }
      } else {
        __ CompareObject(EAX, compiler->bool_true());
        branch->EmitBranchOnCondition(compiler, cond);
      }
    }
    __ jmp(&done);
    __ Bind(&next_test);
  }
  // Fall through leads to deoptimization
  __ jmp(deopt);
  __ Bind(&done);
}


// First test if receiver is NULL, in which case === is applied.
// If type feedback was provided (lists of <class-id, target>), do a
// type by type check (either === or static call to the operator.
static void EmitGenericEqualityCompare(FlowGraphCompiler* compiler,
                                       const LocationSummary& locs,
                                       Token::Kind kind,
                                       BranchInstr* branch,
                                       const ICData& ic_data,
                                       intptr_t cid,
                                       intptr_t token_pos,
                                       intptr_t try_index) {
  ASSERT((kind == Token::kEQ) || (kind == Token::kNE));
  ASSERT(!ic_data.IsNull() && (ic_data.NumberOfChecks() > 0));
  Register left = locs.in(0).reg();
  Register right = locs.in(1).reg();
  const Immediate raw_null =
      Immediate(reinterpret_cast<intptr_t>(Object::null()));
  Label done, non_null_compare;
  __ cmpl(left, raw_null);
  __ j(NOT_EQUAL, &non_null_compare, Assembler::kNearJump);
  // Comparison with NULL is "===".
  __ cmpl(left, right);
  Condition cond = TokenKindToSmiCondition(kind);
  if (branch != NULL) {
    branch->EmitBranchOnCondition(compiler, cond);
  } else {
    Register result = locs.out().reg();
    Label load_true;
    __ j(cond, &load_true, Assembler::kNearJump);
    __ LoadObject(result, compiler->bool_false());
    __ jmp(&done);
    __ Bind(&load_true);
    __ LoadObject(result, compiler->bool_true());
  }
  __ jmp(&done);
  __ Bind(&non_null_compare);  // Receiver is not null.
  __ pushl(left);
  __ pushl(right);
  EmitEqualityAsPolymorphicCall(compiler, ic_data, locs, branch, kind,
                                cid, token_pos, try_index);
  __ Bind(&done);
}


void EqualityCompareComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  if (receiver_class_id() == kSmi) {
    EmitSmiEqualityCompare(compiler, this);
    return;
  }
  if (receiver_class_id() == kDouble) {
    EmitDoubleEqualityCompare(compiler, this);
    return;
  }
  if (HasICData() && (ic_data()->NumberOfChecks() > 0)) {
    EmitGenericEqualityCompare(compiler, *locs(), kind(), NULL,
                               *ic_data(), cid(), token_pos(), try_index());
  } else {
    Register left = locs()->in(0).reg();
    Register right = locs()->in(1).reg();
    __ pushl(left);
    __ pushl(right);
    EmitEqualityAsInstanceCall(compiler, this);
  }
}


LocationSummary* RelationalOpComp::MakeLocationSummary() const {
  if ((operands_class_id() == kSmi) || (operands_class_id() == kDouble)) {
    const intptr_t kNumInputs = 2;
    const intptr_t kNumTemps = 1;
    LocationSummary* summary =
        new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kNoCall);
    summary->set_in(0, Location::RequiresRegister());
    summary->set_in(1, Location::RequiresRegister());
    summary->set_out(Location::RequiresRegister());
    summary->set_temp(0, Location::RequiresRegister());
    return summary;
  }
  ASSERT(operands_class_id() == kObject);
  return MakeCallSummary();
}


static void EmitSmiComparisonOp(FlowGraphCompiler* compiler,
                                const LocationSummary& locs,
                                Token::Kind kind,
                                BranchInstr* branch,
                                intptr_t cid,
                                intptr_t token_pos,
                                intptr_t try_index) {
  Register left = locs.in(0).reg();
  Register right = locs.in(1).reg();
  Register temp = locs.temp(0).reg();
  Label* deopt = compiler->AddDeoptStub(cid,
                                        token_pos,
                                        try_index,
                                        kDeoptSmiCompareSmi,
                                        left,
                                        right);
  __ movl(temp, left);
  __ orl(temp, right);
  __ testl(temp, Immediate(kSmiTagMask));
  __ j(NOT_ZERO, deopt);

  Condition true_condition = TokenKindToSmiCondition(kind);
  __ cmpl(left, right);

  if (branch != NULL) {
    branch->EmitBranchOnCondition(compiler, true_condition);
  } else {
    Register result = locs.out().reg();
    Label done, is_true;
    __ j(true_condition, &is_true);
    __ LoadObject(result, compiler->bool_false());
    __ jmp(&done);
    __ Bind(&is_true);
    __ LoadObject(result, compiler->bool_true());
    __ Bind(&done);
  }
}


static Condition TokenKindToDoubleCondition(Token::Kind kind) {
  switch (kind) {
    case Token::kEQ: return EQUAL;
    case Token::kNE: return NOT_EQUAL;
    case Token::kLT: return BELOW;
    case Token::kGT: return ABOVE;
    case Token::kLTE: return BELOW_EQUAL;
    case Token::kGTE: return ABOVE_EQUAL;
    default:
      UNREACHABLE();
      return OVERFLOW;
  }
}


static void EmitDoubleComparisonOp(FlowGraphCompiler* compiler,
                                   const LocationSummary& locs,
                                   Token::Kind kind,
                                   BranchInstr* branch,
                                   intptr_t cid,
                                   intptr_t token_pos,
                                   intptr_t try_index) {
  Register left = locs.in(0).reg();
  Register right = locs.in(1).reg();
  // TODO(srdjan): temp is only needed if a conversion Smi->Double occurs.
  Register temp = locs.temp(0).reg();
  Label* deopt = compiler->AddDeoptStub(cid,
                                        token_pos,
                                        try_index,
                                        kDeoptDoubleComparison,
                                        left,
                                        right);
  compiler->LoadDoubleOrSmiToXmm(XMM0, left, temp, deopt);
  compiler->LoadDoubleOrSmiToXmm(XMM1, right, temp, deopt);

  Condition true_condition = TokenKindToDoubleCondition(kind);
  if (branch != NULL) {
    compiler->EmitDoubleCompareBranch(
        true_condition, XMM0, XMM1, branch);
  } else {
    compiler->EmitDoubleCompareBool(
        true_condition, XMM0, XMM1, locs.out().reg());
  }
}


void RelationalOpComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  if (operands_class_id() == kSmi) {
    EmitSmiComparisonOp(compiler, *locs(), kind(), NULL,
                        cid(), token_pos(), try_index());
    return;
  }
  if (operands_class_id() == kDouble) {
    EmitDoubleComparisonOp(compiler, *locs(), kind(), NULL,
                           cid(), token_pos(), try_index());
    return;
  }
  if (HasICData() && (ic_data()->NumberOfChecks() > 0)) {
    Label* deopt = compiler->AddDeoptStub(cid(),
                                          token_pos(),
                                          try_index(),
                                          kDeoptRelationalOp);
    // Load receiver into EAX, class into EDI.
    Label done;
    const intptr_t kNumArguments = 2;
    __ movl(EDI, Immediate(kSmi));
    __ movl(EAX, Address(ESP, (kNumArguments - 1) * kWordSize));
    __ testl(EAX, Immediate(kSmiTagMask));
    __ j(ZERO, &done);
    __ LoadClassId(EDI, EAX);
    __ Bind(&done);
    compiler->EmitTestAndCall(ICData::Handle(ic_data()->AsUnaryClassChecks()),
                              EDI,  // Class id register.
                              kNumArguments,
                              Array::Handle(),  // No named arguments.
                              deopt,  // Deoptimize target.
                              NULL,   // Fallthrough when done.
                              cid(),
                              token_pos(),
                              try_index());
    return;
  }
  const String& function_name =
      String::ZoneHandle(Symbols::New(Token::Str(kind())));
  compiler->AddCurrentDescriptor(PcDescriptors::kDeopt,
                                 cid(),
                                 token_pos(),
                                 try_index());
  const intptr_t kNumArguments = 2;
  const intptr_t kNumArgsChecked = 2;  // Type-feedback.
  compiler->GenerateInstanceCall(cid(),
                                 token_pos(),
                                 try_index(),
                                 function_name,
                                 kNumArguments,
                                 Array::ZoneHandle(),  // No optional arguments.
                                 kNumArgsChecked);
  ASSERT(locs()->out().reg() == EAX);
}


LocationSummary* NativeCallComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 0;
  const intptr_t kNumTemps = 3;
  LocationSummary* locs =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
  locs->set_temp(0, Location::RegisterLocation(EAX));
  locs->set_temp(1, Location::RegisterLocation(ECX));
  locs->set_temp(2, Location::RegisterLocation(EDX));
  locs->set_out(Location::RegisterLocation(EAX));
  return locs;
}


void NativeCallComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(locs()->temp(0).reg() == EAX);
  ASSERT(locs()->temp(1).reg() == ECX);
  ASSERT(locs()->temp(2).reg() == EDX);
  Register result = locs()->out().reg();

  // Push the result place holder initialized to NULL.
  __ PushObject(Object::ZoneHandle());
  // Pass a pointer to the first argument in EAX.
  intptr_t arg_count = argument_count();
  if (is_native_instance_closure()) {
    arg_count += 1;
  }
  if (!has_optional_parameters() && !is_native_instance_closure()) {
    __ leal(EAX, Address(EBP, (1 + arg_count) * kWordSize));
  } else {
    __ leal(EAX,
            Address(EBP, ParsedFunction::kFirstLocalSlotIndex * kWordSize));
  }
  __ movl(ECX, Immediate(reinterpret_cast<uword>(native_c_function())));
  __ movl(EDX, Immediate(arg_count));
  compiler->GenerateCall(token_pos(),
                         try_index(),
                         &StubCode::CallNativeCFunctionLabel(),
                         PcDescriptors::kOther);
  __ popl(result);
}


LocationSummary* LoadIndexedComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 2;
  if ((receiver_type() == kGrowableObjectArray) ||
      (receiver_type() == kArray) ||
      (receiver_type() == kImmutableArray)) {
    const intptr_t kNumTemps = 1;
    LocationSummary* locs =
        new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kNoCall);
    locs->set_in(0, Location::RequiresRegister());
    locs->set_in(1, Location::RequiresRegister());
    locs->set_temp(0, Location::RequiresRegister());
    locs->set_out(Location::RequiresRegister());
    return locs;
  } else {
    ASSERT(receiver_type() == kIllegalObjectKind);
    return MakeCallSummary();
  }
}


static void EmitLoadIndexedPolymorphic(FlowGraphCompiler* compiler,
                                        LoadIndexedComp* comp) {
  Label* deopt = compiler->AddDeoptStub(comp->cid(),
                                        comp->token_pos(),
                                        comp->try_index(),
                                        kDeoptLoadIndexedPolymorphic);
  ASSERT(comp->ic_data()->NumberOfChecks() > 0);
  ASSERT(comp->HasICData());
  const ICData& ic_data = *comp->ic_data();
  ASSERT(ic_data.num_args_tested() == 1);
  // No indexed access on Smi.
  ASSERT(ic_data.GetReceiverClassIdAt(0) != kSmi);
  // Load receiver into EAX.
  const intptr_t kNumArguments = 2;
  __ movl(EAX, Address(ESP, (kNumArguments - 1) * kWordSize));
  __ testl(EAX, Immediate(kSmiTagMask));
  __ j(ZERO, deopt);
  __ LoadClassId(EDI, EAX);
  compiler->EmitTestAndCall(ic_data,
                            EDI,  // Class id register.
                            kNumArguments,
                            Array::Handle(),  // No named arguments.
                            deopt,  // Deoptimize target.
                            NULL,   // Fallthrough when done.
                            comp->cid(),
                            comp->token_pos(),
                            comp->try_index());
}


void LoadIndexedComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  if (receiver_type() == kIllegalObjectKind) {
    if (HasICData() && (ic_data()->NumberOfChecks() > 0)) {
      EmitLoadIndexedPolymorphic(compiler, this);
    } else {
      compiler->EmitLoadIndexedGeneric(this);
    }
    ASSERT(locs()->out().reg() == EAX);
    return;
  }

  Register receiver = locs()->in(0).reg();
  Register index = locs()->in(1).reg();
  Register result = locs()->out().reg();
  Register temp = locs()->temp(0).reg();

  const DeoptReasonId deopt_reason = (receiver_type() == kGrowableObjectArray) ?
      kDeoptLoadIndexedGrowableArray : kDeoptLoadIndexedFixedArray;

  Label* deopt = compiler->AddDeoptStub(cid(),
                                        token_pos(),
                                        try_index(),
                                        deopt_reason,
                                        receiver,
                                        index);

  __ testl(receiver, Immediate(kSmiTagMask));  // Deoptimize if Smi.
  __ j(ZERO, deopt);
  __ CompareClassId(receiver, receiver_type(), temp);
  __ j(NOT_EQUAL, deopt);

  __ testl(index, Immediate(kSmiTagMask));
  __ j(NOT_ZERO, deopt);

  switch (receiver_type()) {
    case kArray:
    case kImmutableArray:
      __ cmpl(index, FieldAddress(receiver, Array::length_offset()));
      __ j(ABOVE_EQUAL, deopt);
      // Note that index is Smi, i.e, times 2.
      ASSERT(kSmiTagShift == 1);
      __ movl(result, FieldAddress(receiver, index, TIMES_2, sizeof(RawArray)));
      break;

    case kGrowableObjectArray: {
      Register temp = locs()->temp(0).reg();

      __ cmpl(index,
              FieldAddress(receiver, GrowableObjectArray::length_offset()));
      __ j(ABOVE_EQUAL, deopt);
      __ movl(temp, FieldAddress(receiver, GrowableObjectArray::data_offset()));
      // Note that index is Smi, i.e, times 2.
      ASSERT(kSmiTagShift == 1);
      __ movl(result, FieldAddress(temp, index, TIMES_2, sizeof(RawArray)));
      break;
    }

    default:
      UNREACHABLE();
      break;
  }
}


LocationSummary* StoreIndexedComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 3;
  if ((receiver_type() == kGrowableObjectArray) ||
      (receiver_type() == kArray)) {
    const intptr_t kNumTemps = 1;
    LocationSummary* locs =
        new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kNoCall);
    locs->set_in(0, Location::RequiresRegister());
    locs->set_in(1, Location::RequiresRegister());
    locs->set_in(2, Location::RequiresRegister());
    locs->set_temp(0, Location::RequiresRegister());
    locs->set_out(Location::NoLocation());
    return locs;
  } else {
    ASSERT(receiver_type() == kIllegalObjectKind);
    return MakeCallSummary();
  }
}


static void EmitStoreIndexedGeneric(FlowGraphCompiler* compiler,
                                    StoreIndexedComp* comp) {
  const String& function_name =
      String::ZoneHandle(Symbols::New(Token::Str(Token::kASSIGN_INDEX)));

  compiler->AddCurrentDescriptor(PcDescriptors::kDeopt,
                                 comp->cid(),
                                 comp->token_pos(),
                                 comp->try_index());

  const intptr_t kNumArguments = 3;
  const intptr_t kNumArgsChecked = 1;  // Type-feedback.
  compiler->GenerateInstanceCall(comp->cid(),
                                 comp->token_pos(),
                                 comp->try_index(),
                                 function_name,
                                 kNumArguments,
                                 Array::ZoneHandle(),  // No named arguments.
                                 kNumArgsChecked);
}


static void EmitStoreIndexedPolymorphic(FlowGraphCompiler* compiler,
                                        StoreIndexedComp* comp) {
  Label* deopt = compiler->AddDeoptStub(comp->cid(),
                                        comp->token_pos(),
                                        comp->try_index(),
                                        kDeoptStoreIndexedPolymorphic);
  ASSERT(comp->ic_data()->NumberOfChecks() > 0);
  ASSERT(comp->HasICData());
  const ICData& ic_data = *comp->ic_data();
  ASSERT(ic_data.num_args_tested() == 1);
  // No indexed access on Smi.
  ASSERT(ic_data.GetReceiverClassIdAt(0) != kSmi);
  // Load receiver into EAX.
  const intptr_t kNumArguments = 3;
  __ movl(EAX, Address(ESP, (kNumArguments - 1) * kWordSize));
  __ testl(EAX, Immediate(kSmiTagMask));
  __ j(ZERO, deopt);
  __ LoadClassId(EDI, EAX);
  compiler->EmitTestAndCall(ic_data,
                            EDI,  // Class id register.
                            kNumArguments,
                            Array::Handle(),  // No named arguments.
                            deopt,  // Deoptimize target.
                            NULL,   // Fallthrough when done.
                            comp->cid(),
                            comp->token_pos(),
                            comp->try_index());
}


void StoreIndexedComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  if (receiver_type() == kIllegalObjectKind) {
    if (HasICData() && (ic_data()->NumberOfChecks() > 0)) {
      EmitStoreIndexedPolymorphic(compiler, this);
    } else {
      EmitStoreIndexedGeneric(compiler, this);
    }
    return;
  }

  Register receiver = locs()->in(0).reg();
  Register index = locs()->in(1).reg();
  Register value = locs()->in(2).reg();
  Register temp = locs()->temp(0).reg();

  Label* deopt = compiler->AddDeoptStub(cid(),
                                        token_pos(),
                                        try_index(),
                                        kDeoptStoreIndexed,
                                        receiver,
                                        index,
                                        value);

  __ testl(receiver, Immediate(kSmiTagMask));  // Deoptimize if Smi.
  __ j(ZERO, deopt);
  __ CompareClassId(receiver, receiver_type(), temp);
  __ j(NOT_EQUAL, deopt);

  __ testl(index, Immediate(kSmiTagMask));
  __ j(NOT_ZERO, deopt);

  switch (receiver_type()) {
    case kArray:
    case kImmutableArray:
      __ cmpl(index, FieldAddress(receiver, Array::length_offset()));
      __ j(ABOVE_EQUAL, deopt);
      // Note that index is Smi, i.e, times 2.
      ASSERT(kSmiTagShift == 1);
      __ StoreIntoObject(receiver,
          FieldAddress(receiver, index, TIMES_2, sizeof(RawArray)),
          value);
      break;

    case kGrowableObjectArray: {
      __ cmpl(index,
              FieldAddress(receiver, GrowableObjectArray::length_offset()));
      __ j(ABOVE_EQUAL, deopt);
      __ movl(temp, FieldAddress(receiver, GrowableObjectArray::data_offset()));
      // Note that index is Smi, i.e, times 2.
      ASSERT(kSmiTagShift == 1);
      __ StoreIntoObject(temp,
          FieldAddress(temp, index, TIMES_2, sizeof(RawArray)),
          value);
      break;
    }

    default:
      UNREACHABLE();
      break;
  }
}


LocationSummary* LoadInstanceFieldComp::MakeLocationSummary() const {
  // TODO(fschneider): For this instruction the input register may be
  // reused for the result (but is not required to) because the input
  // is not used after the result is defined.  We should consider adding
  // this information to the input policy.
  return LocationSummary::Make(1,
                               Location::RequiresRegister(),
                               LocationSummary::kNoCall);
}


void LoadInstanceFieldComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register instance_reg = locs()->in(0).reg();
  Register result_reg = locs()->out().reg();

  if (HasICData()) {
    ASSERT(original() != NULL);
    Label* deopt = compiler->AddDeoptStub(original()->cid(),
                                          original()->token_pos(),
                                          original()->try_index(),
                                          kDeoptInstanceGetterSameTarget,
                                          instance_reg);
    // Smis do not have instance fields (Smi class is always first).
    // Use 'result' as temporary register.
    ASSERT(result_reg != instance_reg);
    ASSERT(ic_data() != NULL);
    compiler->EmitClassChecksNoSmi(*ic_data(), instance_reg, result_reg, deopt);
  }
  __ movl(result_reg, FieldAddress(instance_reg, field().Offset()));
}


LocationSummary* LoadStaticFieldComp::MakeLocationSummary() const {
  return LocationSummary::Make(0,
                               Location::RequiresRegister(),
                               LocationSummary::kNoCall);
}


void LoadStaticFieldComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register result = locs()->out().reg();
  __ LoadObject(result, field());
  __ movl(result, FieldAddress(result, Field::value_offset()));
}


LocationSummary* InstanceOfComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 3;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
  summary->set_in(0, Location::RegisterLocation(EAX));
  summary->set_in(1, Location::RegisterLocation(ECX));
  summary->set_in(2, Location::RegisterLocation(EDX));
  summary->set_out(Location::RegisterLocation(EAX));
  return summary;
}


void InstanceOfComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(locs()->in(0).reg() == EAX);  // Value.
  ASSERT(locs()->in(1).reg() == ECX);  // Instantiator.
  ASSERT(locs()->in(2).reg() == EDX);  // Instantiator type arguments.

  compiler->GenerateInstanceOf(cid(),
                               token_pos(),
                               try_index(),
                               type(),
                               negate_result());
  ASSERT(locs()->out().reg() == EAX);
}


LocationSummary* CreateArrayComp::MakeLocationSummary() const {
  // TODO(regis): The elements of the array could be considered as arguments to
  // CreateArrayComp, thereby making CreateArrayComp a call.
  // For VerifyCallComputation to work, CreateArrayComp would need an
  // ArgumentCount getter and an ArgumentAt getter.
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 1;
  LocationSummary* locs =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
  locs->set_in(0, Location::RegisterLocation(ECX));
  locs->set_temp(0, Location::RegisterLocation(EDX));
  locs->set_out(Location::RegisterLocation(EAX));
  return locs;
}


void CreateArrayComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register temp_reg = locs()->temp(0).reg();
  Register result_reg = locs()->out().reg();

  // Allocate the array.  EDX = length, ECX = element type.
  ASSERT(temp_reg == EDX);
  ASSERT(locs()->in(0).reg() == ECX);
  __ movl(temp_reg,  Immediate(Smi::RawValue(ElementCount())));
  compiler->GenerateCall(token_pos(),
                         try_index(),
                         &StubCode::AllocateArrayLabel(),
                         PcDescriptors::kOther);
  ASSERT(result_reg == EAX);

  // Pop the element values from the stack into the array.
  __ leal(temp_reg, FieldAddress(result_reg, Array::data_offset()));
  for (int i = ElementCount() - 1; i >= 0; --i) {
    ASSERT(ElementAt(i)->IsUse());
    __ popl(Address(temp_reg, i * kWordSize));
  }
}


LocationSummary*
    AllocateObjectWithBoundsCheckComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  LocationSummary* locs =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
  locs->set_in(0, Location::RegisterLocation(EAX));
  locs->set_in(1, Location::RegisterLocation(ECX));
  locs->set_out(Location::RegisterLocation(EAX));
  return locs;
}


void AllocateObjectWithBoundsCheckComp::EmitNativeCode(
    FlowGraphCompiler* compiler) {
  const Class& cls = Class::ZoneHandle(constructor().owner());
  Register type_arguments = locs()->in(0).reg();
  Register instantiator_type_arguments = locs()->in(1).reg();
  Register result = locs()->out().reg();

  // Push the result place holder initialized to NULL.
  __ PushObject(Object::ZoneHandle());
  __ PushObject(cls);
  __ pushl(type_arguments);
  __ pushl(instantiator_type_arguments);
  compiler->GenerateCallRuntime(cid(),
                                token_pos(),
                                try_index(),
                                kAllocateObjectWithBoundsCheckRuntimeEntry);
  // Pop instantiator type arguments, type arguments, and class.
  // source location.
  __ Drop(3);
  __ popl(result);  // Pop new instance.
}


LocationSummary* LoadVMFieldComp::MakeLocationSummary() const {
  return LocationSummary::Make(1,
                               Location::RequiresRegister(),
                               LocationSummary::kNoCall);
}


void LoadVMFieldComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register instance_reg = locs()->in(0).reg();
  Register result_reg = locs()->out().reg();
  if (HasICData()) {
    ASSERT(original() != NULL);
    Label* deopt = compiler->AddDeoptStub(original()->cid(),
                                          original()->token_pos(),
                                          original()->try_index(),
                                          kDeoptInstanceGetterSameTarget,
                                          instance_reg);
    // Smis do not have instance fields (Smi class is always first).
    // Use 'result' as temporary register.
    ASSERT(result_reg != instance_reg);
    ASSERT(ic_data() != NULL);
    compiler->EmitClassChecksNoSmi(*ic_data(), instance_reg, result_reg, deopt);
  }

  __ movl(result_reg, FieldAddress(instance_reg, offset_in_bytes()));
}


LocationSummary* InstantiateTypeArgumentsComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 1;
  LocationSummary* locs =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
  locs->set_in(0, Location::RegisterLocation(EAX));
  locs->set_temp(0, Location::RegisterLocation(ECX));
  locs->set_out(Location::RegisterLocation(EAX));
  return locs;
}


void InstantiateTypeArgumentsComp::EmitNativeCode(
    FlowGraphCompiler* compiler) {
  Register instantiator_reg = locs()->in(0).reg();
  Register temp = locs()->temp(0).reg();
  Register result_reg = locs()->out().reg();

  // 'instantiator_reg' is the instantiator AbstractTypeArguments object
  // (or null).
  // If the instantiator is null and if the type argument vector
  // instantiated from null becomes a vector of Dynamic, then use null as
  // the type arguments.
  Label type_arguments_instantiated;
  const intptr_t len = type_arguments().Length();
  if (type_arguments().IsRawInstantiatedRaw(len)) {
    const Immediate raw_null =
        Immediate(reinterpret_cast<intptr_t>(Object::null()));
    __ cmpl(instantiator_reg, raw_null);
    __ j(EQUAL, &type_arguments_instantiated, Assembler::kNearJump);
  }
  // Instantiate non-null type arguments.
  if (type_arguments().IsUninstantiatedIdentity()) {
    // Check if the instantiator type argument vector is a TypeArguments of a
    // matching length and, if so, use it as the instantiated type_arguments.
    // No need to check the instantiator ('instantiator_reg') for null here,
    // because a null instantiator will have the wrong class (Null instead of
    // TypeArguments).
    Label type_arguments_uninstantiated;
    __ CompareClassId(instantiator_reg, kTypeArguments, temp);
    __ j(NOT_EQUAL, &type_arguments_uninstantiated, Assembler::kNearJump);
    __ cmpl(FieldAddress(instantiator_reg, TypeArguments::length_offset()),
            Immediate(Smi::RawValue(len)));
    __ j(EQUAL, &type_arguments_instantiated, Assembler::kNearJump);
    __ Bind(&type_arguments_uninstantiated);
  }
  // A runtime call to instantiate the type arguments is required.
  __ PushObject(Object::ZoneHandle());  // Make room for the result.
  __ PushObject(type_arguments());
  __ pushl(instantiator_reg);  // Push instantiator type arguments.
  compiler->GenerateCallRuntime(cid(),
                                token_pos(),
                                try_index(),
                                kInstantiateTypeArgumentsRuntimeEntry);
  __ Drop(2);  // Drop instantiator and uninstantiated type arguments.
  __ popl(result_reg);  // Pop instantiated type arguments.
  __ Bind(&type_arguments_instantiated);
  ASSERT(instantiator_reg == result_reg);
  // 'result_reg': Instantiated type arguments.
}


LocationSummary*
    ExtractConstructorTypeArgumentsComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 1;
  LocationSummary* locs =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kNoCall);
  locs->set_in(0, Location::RequiresRegister());
  locs->set_out(Location::SameAsFirstInput());
  locs->set_temp(0, Location::RequiresRegister());
  return locs;
}


void ExtractConstructorTypeArgumentsComp::EmitNativeCode(
    FlowGraphCompiler* compiler) {
  Register instantiator_reg = locs()->in(0).reg();
  Register result_reg = locs()->out().reg();
  ASSERT(instantiator_reg == result_reg);
  Register temp_reg = locs()->temp(0).reg();

  // instantiator_reg is the instantiator type argument vector, i.e. an
  // AbstractTypeArguments object (or null).
  // If the instantiator is null and if the type argument vector
  // instantiated from null becomes a vector of Dynamic, then use null as
  // the type arguments.
  Label type_arguments_instantiated;
  const intptr_t len = type_arguments().Length();
  if (type_arguments().IsRawInstantiatedRaw(len)) {
    const Immediate raw_null =
        Immediate(reinterpret_cast<intptr_t>(Object::null()));
    __ cmpl(instantiator_reg, raw_null);
    __ j(EQUAL, &type_arguments_instantiated, Assembler::kNearJump);
  }
  // Instantiate non-null type arguments.
  if (type_arguments().IsUninstantiatedIdentity()) {
    // Check if the instantiator type argument vector is a TypeArguments of a
    // matching length and, if so, use it as the instantiated type_arguments.
    // No need to check instantiator_reg for null here, because a null
    // instantiator will have the wrong class (Null instead of TypeArguments).
    Label type_arguments_uninstantiated;
    __ CompareClassId(instantiator_reg, kTypeArguments, temp_reg);
    __ j(NOT_EQUAL, &type_arguments_uninstantiated, Assembler::kNearJump);
    Immediate arguments_length =
        Immediate(Smi::RawValue(type_arguments().Length()));
    __ cmpl(FieldAddress(instantiator_reg, TypeArguments::length_offset()),
        arguments_length);
    __ j(EQUAL, &type_arguments_instantiated, Assembler::kNearJump);
    __ Bind(&type_arguments_uninstantiated);
  }
  // In the non-factory case, we rely on the allocation stub to
  // instantiate the type arguments.
  __ LoadObject(result_reg, type_arguments());
  // result_reg: uninstantiated type arguments.
  __ Bind(&type_arguments_instantiated);
  // result_reg: uninstantiated or instantiated type arguments.
}


LocationSummary*
    ExtractConstructorInstantiatorComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 1;
  LocationSummary* locs =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kNoCall);
  locs->set_in(0, Location::RequiresRegister());
  locs->set_out(Location::SameAsFirstInput());
  locs->set_temp(0, Location::RequiresRegister());
  return locs;
}


void ExtractConstructorInstantiatorComp::EmitNativeCode(
    FlowGraphCompiler* compiler) {
  ASSERT(instantiator()->IsUse());
  Register instantiator_reg = locs()->in(0).reg();
  ASSERT(locs()->out().reg() == instantiator_reg);
  Register temp_reg = locs()->temp(0).reg();

  // instantiator_reg is the instantiator AbstractTypeArguments object
  // (or null).  If the instantiator is null and if the type argument vector
  // instantiated from null becomes a vector of Dynamic, then use null as
  // the type arguments and do not pass the instantiator.
  Label done;
  const intptr_t len = type_arguments().Length();
  if (type_arguments().IsRawInstantiatedRaw(len)) {
    const Immediate raw_null =
        Immediate(reinterpret_cast<intptr_t>(Object::null()));
    Label instantiator_not_null;
    __ cmpl(instantiator_reg, raw_null);
    __ j(NOT_EQUAL, &instantiator_not_null, Assembler::kNearJump);
    // Null was used in VisitExtractConstructorTypeArguments as the
    // instantiated type arguments, no proper instantiator needed.
    __ movl(instantiator_reg,
            Immediate(Smi::RawValue(StubCode::kNoInstantiator)));
    __ jmp(&done);
    __ Bind(&instantiator_not_null);
  }
  // Instantiate non-null type arguments.
  if (type_arguments().IsUninstantiatedIdentity()) {
    // TODO(regis): The following emitted code is duplicated in
    // VisitExtractConstructorTypeArguments above. The reason is that the code
    // is split between two computations, so that each one produces a
    // single value, rather than producing a pair of values.
    // If this becomes an issue, we should expose these tests at the IL level.

    // Check if the instantiator type argument vector is a TypeArguments of a
    // matching length and, if so, use it as the instantiated type_arguments.
    // No need to check the instantiator (RAX) for null here, because a null
    // instantiator will have the wrong class (Null instead of TypeArguments).
    __ CompareClassId(instantiator_reg, kTypeArguments, temp_reg);
    __ j(NOT_EQUAL, &done, Assembler::kNearJump);
    Immediate arguments_length =
        Immediate(Smi::RawValue(type_arguments().Length()));
    __ cmpl(FieldAddress(instantiator_reg, TypeArguments::length_offset()),
        arguments_length);
    __ j(NOT_EQUAL, &done, Assembler::kNearJump);
    // The instantiator was used in VisitExtractConstructorTypeArguments as the
    // instantiated type arguments, no proper instantiator needed.
    __ movl(instantiator_reg,
            Immediate(Smi::RawValue(StubCode::kNoInstantiator)));
  }
  __ Bind(&done);
  // instantiator_reg: instantiator or kNoInstantiator.
}


LocationSummary* AllocateContextComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 0;
  const intptr_t kNumTemps = 1;
  LocationSummary* locs =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
  locs->set_temp(0, Location::RegisterLocation(EDX));
  locs->set_out(Location::RegisterLocation(EAX));
  return locs;
}


void AllocateContextComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(locs()->temp(0).reg() == EDX);
  ASSERT(locs()->out().reg() == EAX);

  __ movl(EDX, Immediate(num_context_variables()));
  const ExternalLabel label("alloc_context",
                            StubCode::AllocateContextEntryPoint());
  compiler->GenerateCall(token_pos(),
                         try_index(),
                         &label,
                         PcDescriptors::kOther);
}


LocationSummary* CloneContextComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* locs =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
  locs->set_in(0, Location::RegisterLocation(EAX));
  locs->set_out(Location::RegisterLocation(EAX));
  return locs;
}


void CloneContextComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register context_value = locs()->in(0).reg();
  Register result = locs()->out().reg();

  __ PushObject(Object::ZoneHandle());  // Make room for the result.
  __ pushl(context_value);
  compiler->GenerateCallRuntime(cid(),
                                token_pos(),
                                try_index(),
                                kCloneContextRuntimeEntry);
  __ popl(result);  // Remove argument.
  __ popl(result);  // Get result (cloned context).
}


LocationSummary* CatchEntryComp::MakeLocationSummary() const {
  return LocationSummary::Make(0,
                               Location::NoLocation(),
                               LocationSummary::kNoCall);
}


// Restore stack and initialize the two exception variables:
// exception and stack trace variables.
void CatchEntryComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  // Restore RSP from RBP as we are coming from a throw and the code for
  // popping arguments has not been run.
  const intptr_t locals_space_size = compiler->StackSize() * kWordSize;
  ASSERT(locals_space_size >= 0);
  const intptr_t offset_size =
      -locals_space_size + FlowGraphCompiler::kLocalsOffsetFromFP;
  __ leal(ESP, Address(EBP, offset_size));

  ASSERT(!exception_var().is_captured());
  ASSERT(!stacktrace_var().is_captured());
  __ movl(Address(EBP, exception_var().index() * kWordSize),
          kExceptionObjectReg);
  __ movl(Address(EBP, stacktrace_var().index() * kWordSize),
          kStackTraceObjectReg);
}


LocationSummary* CheckStackOverflowComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 0;
  const intptr_t kNumTemps = 0;
  // TODO(vegorov): spilling is required only on an infrequently executed path.
  LocationSummary* summary =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
  return summary;
}


void CheckStackOverflowComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  __ cmpl(ESP,
          Address::Absolute(Isolate::Current()->stack_limit_address()));
  Label no_stack_overflow;
  __ j(ABOVE, &no_stack_overflow);
  compiler->GenerateCallRuntime(cid(),
                                token_pos(),
                                try_index(),
                                kStackOverflowRuntimeEntry);
  __ Bind(&no_stack_overflow);
}


LocationSummary* BinaryOpComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 2;

  // Double operation are handled in DoubleBinaryOpComp.
  ASSERT(operands_type() != kDoubleOperands);

  if (operands_type() == kMintOperands) {
    ASSERT(op_kind() == Token::kBIT_AND);
    const intptr_t kNumTemps = 1;
    LocationSummary* summary =
        new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
    summary->set_in(0, Location::RegisterLocation(EAX));
    summary->set_in(1, Location::RegisterLocation(ECX));
    summary->set_temp(0, Location::RegisterLocation(EDX));
    summary->set_out(Location::RegisterLocation(EAX));
    return summary;
  }

  ASSERT(operands_type() == kSmiOperands);

  if (op_kind() == Token::kTRUNCDIV) {
    const intptr_t kNumTemps = 3;
    LocationSummary* summary =
        new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kNoCall);
    summary->set_in(0, Location::RegisterLocation(EAX));
    summary->set_in(1, Location::RegisterLocation(ECX));
    summary->set_out(Location::SameAsFirstInput());
    summary->set_temp(0, Location::RegisterLocation(EBX));
    // Will be used for for sign extension.
    summary->set_temp(1, Location::RegisterLocation(EDX));
    summary->set_temp(2, Location::RequiresRegister());
    return summary;
  } else if (op_kind() == Token::kSHR) {
    const intptr_t kNumTemps = 1;
    LocationSummary* summary =
        new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kNoCall);
    summary->set_in(0, Location::RequiresRegister());
    summary->set_in(1, Location::RegisterLocation(ECX));
    summary->set_out(Location::SameAsFirstInput());
    summary->set_temp(0, Location::RequiresRegister());
    return summary;
  } else if (op_kind() == Token::kSHL) {
    // Two Smi operands can easily overflow into Mint.
    const intptr_t kNumTemps = 2;
    LocationSummary* summary =
        new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
    summary->set_in(0, Location::RegisterLocation(EAX));
    summary->set_in(1, Location::RegisterLocation(EDX));
    summary->set_temp(0, Location::RegisterLocation(EBX));
    summary->set_temp(1, Location::RegisterLocation(ECX));
    summary->set_out(Location::RegisterLocation(EAX));
    return summary;
  } else {
    const intptr_t kNumTemps = 1;
    LocationSummary* summary =
        new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kNoCall);
    summary->set_in(0, Location::RequiresRegister());
    summary->set_in(1, Location::RequiresRegister());
    summary->set_out(Location::SameAsFirstInput());
    summary->set_temp(0, Location::RequiresRegister());
    return summary;
  }
}


static void EmitSmiBinaryOp(FlowGraphCompiler* compiler, BinaryOpComp* comp) {
  Register left = comp->locs()->in(0).reg();
  Register right = comp->locs()->in(1).reg();
  Register result = comp->locs()->out().reg();
  Register temp = comp->locs()->temp(0).reg();
  ASSERT(left == result);
  Label* deopt = compiler->AddDeoptStub(comp->instance_call()->cid(),
                                        comp->instance_call()->token_pos(),
                                        comp->instance_call()->try_index(),
                                        kDeoptSmiBinaryOp,
                                        temp,
                                        right);
  // TODO(vegorov): for many binary operations this pattern can be rearranged
  // to save one move.
  __ movl(temp, left);
  __ orl(left, right);
  __ testl(left, Immediate(kSmiTagMask));
  __ j(NOT_ZERO, deopt);
  __ movl(left, temp);
  switch (comp->op_kind()) {
    case Token::kADD: {
      __ addl(left, right);
      __ j(OVERFLOW, deopt);
      break;
    }
    case Token::kSUB: {
      __ subl(left, right);
      __ j(OVERFLOW, deopt);
      break;
    }
    case Token::kMUL: {
      __ SmiUntag(left);
      __ imull(left, right);
      __ j(OVERFLOW, deopt);
      break;
    }
    case Token::kBIT_AND: {
      // No overflow check.
      __ andl(left, right);
      break;
    }
    case Token::kBIT_OR: {
      // No overflow check.
      __ orl(left, right);
      break;
    }
    case Token::kBIT_XOR: {
      // No overflow check.
      __ xorl(left, right);
      break;
    }
    case Token::kTRUNCDIV: {
      // Handle divide by zero in runtime.
      // Deoptimization requires that temp and right are preserved.
      __ testl(right, right);
      __ j(ZERO, deopt);
      ASSERT(left == EAX);
      ASSERT((right != EDX) && (right != EAX));
      ASSERT((temp != EDX) && (temp != EAX));
      ASSERT(comp->locs()->temp(1).reg() == EDX);
      ASSERT(result == EAX);
      Register right_temp = comp->locs()->temp(2).reg();
      __ movl(right_temp, right);
      __ SmiUntag(left);
      __ SmiUntag(right_temp);
      __ cdq();  // Sign extend EAX -> EDX:EAX.
      __ idivl(right_temp);  //  EAX: quotient, EDX: remainder.
      // Check the corner case of dividing the 'MIN_SMI' with -1, in which
      // case we cannot tag the result.
      __ cmpl(result, Immediate(0x40000000));
      __ j(EQUAL, deopt);
      __ SmiTag(result);
      break;
    }
    case Token::kSHR: {
      // sarl operation masks the count to 5 bits.
      const Immediate kCountLimit = Immediate(0x1F);
      __ cmpl(right, Immediate(0));
      __ j(LESS, deopt);
      __ SmiUntag(right);
      __ cmpl(right, kCountLimit);
      Label count_ok;
      __ j(LESS, &count_ok, Assembler::kNearJump);
      __ movl(right, kCountLimit);
      __ Bind(&count_ok);
      ASSERT(right == ECX);  // Count must be in ECX
      __ SmiUntag(left);
      __ sarl(left, right);
      __ SmiTag(left);
      break;
    }
    case Token::kSHL: {
      Label call_method, done;
      // Check if count too large for handling it inlined.
      __ cmpl(right,
          Immediate(reinterpret_cast<int64_t>(Smi::New(Smi::kBits))));
      __ j(ABOVE_EQUAL, &call_method, Assembler::kNearJump);
      Register right_temp = comp->locs()->temp(1).reg();
      ASSERT(right_temp == ECX);  // Count must be in ECX
      __ movl(right_temp, right);
      __ SmiUntag(right_temp);
      // Overflow test (preserve temp and right);
      __ shll(left, right_temp);
      __ sarl(left, right_temp);
      __ cmpl(left, temp);
      __ j(NOT_EQUAL, &call_method, Assembler::kNearJump);  // Overflow.
      // Shift for result now we know there is no overflow.
      __ shll(left, right_temp);
      __ jmp(&done);
      {
        __ Bind(&call_method);
        Function& target = Function::ZoneHandle(
            comp->ic_data()->GetTargetForReceiverClassId(kSmi));
        ASSERT(!target.IsNull());
        const intptr_t kArgumentCount = 2;
        __ pushl(temp);
        __ pushl(right);
        compiler->GenerateStaticCall(comp->instance_call()->cid(),
                                     comp->instance_call()->token_pos(),
                                     comp->instance_call()->try_index(),
                                     target,
                                     kArgumentCount,
                                     Array::Handle());  // No argument names.
        ASSERT(result == EAX);
      }
      __ Bind(&done);
      break;
    }
    case Token::kDIV: {
      // Dispatches to 'Double./'.
      // TODO(srdjan): Implement as conversion to double and double division.
      UNREACHABLE();
      break;
    }
    case Token::kMOD: {
      // TODO(srdjan): Implement.
      UNREACHABLE();
      break;
    }
    case Token::kOR:
    case Token::kAND: {
      // Flow graph builder has dissected this operation to guarantee correct
      // behavior (short-circuit evaluation).
      UNREACHABLE();
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}


static void EmitMintBinaryOp(FlowGraphCompiler* compiler, BinaryOpComp* comp) {
  // TODO(regis): For now, we only support Token::kBIT_AND for a Mint or Smi
  // receiver and a Mint or Smi argument. We fall back to the run time call if
  // both receiver and argument are Mint or if one of them is Mint and the other
  // is a negative Smi.
  Register left = comp->locs()->in(0).reg();
  Register right = comp->locs()->in(1).reg();
  Register result = comp->locs()->out().reg();
  Register temp = comp->locs()->temp(0).reg();
  ASSERT(left == result);
  ASSERT(comp->op_kind() == Token::kBIT_AND);
  Label* deopt = compiler->AddDeoptStub(comp->instance_call()->cid(),
                                        comp->instance_call()->token_pos(),
                                        comp->instance_call()->try_index(),
                                        kDeoptMintBinaryOp,
                                        left,
                                        right);
  Label mint_static_call, smi_static_call, non_smi, smi_smi, done;
  __ testl(left, Immediate(kSmiTagMask));  // Is receiver Smi?
  __ j(NOT_ZERO, &non_smi);
  __ testl(right, Immediate(kSmiTagMask));  // Is argument Smi?
  __ j(ZERO, &smi_smi);
  __ CompareClassId(right, kMint, temp);  // Is argument Mint?
  __ j(NOT_EQUAL, deopt);  // Argument neither Smi nor Mint.
  __ cmpl(left, Immediate(0));
  __ j(LESS, &smi_static_call);  // Negative Smi receiver, Mint argument.

  // Positive Smi receiver, Mint argument.
  // Load lower argument Mint word, convert to Smi. It is OK to loose bits.
  __ movl(right, FieldAddress(right, Mint::value_offset()));
  __ SmiTag(right);
  __ andl(result, right);
  __ jmp(&done);

  __ Bind(&non_smi);  // Receiver is non-Smi.
  __ CompareClassId(left, kMint, temp);  // Is receiver Mint?
  __ j(NOT_EQUAL, deopt);  // Receiver neither Smi nor Mint.
  __ testl(right, Immediate(kSmiTagMask));  // Is argument Smi?
  __ j(NOT_ZERO, &mint_static_call);  // Mint receiver, non-Smi argument.
  __ cmpl(right, Immediate(0));
  __ j(LESS, &mint_static_call);  // Mint receiver, negative Smi argument.

  // Mint receiver, positive Smi argument.
  // Load lower receiver Mint word, convert to Smi. It is OK to loose bits.
  __ movl(result, FieldAddress(left, Mint::value_offset()));
  __ SmiTag(result);
  __ Bind(&smi_smi);
  __ andl(result, right);
  __ jmp(&done);

  __ Bind(&smi_static_call);
  {
    Function& target = Function::ZoneHandle(
        comp->ic_data()->GetTargetForReceiverClassId(kSmi));
    if (target.IsNull()) {
      __ jmp(deopt);
    } else {
      __ pushl(left);
      __ pushl(right);
      compiler->GenerateStaticCall(comp->instance_call()->cid(),
                                   comp->instance_call()->token_pos(),
                                   comp->instance_call()->try_index(),
                                   target,
                                   comp->instance_call()->ArgumentCount(),
                                   comp->instance_call()->argument_names());
      ASSERT(result == EAX);
      __ jmp(&done);
    }
  }

  __ Bind(&mint_static_call);
  {
    Function& target = Function::ZoneHandle(
        comp->ic_data()->GetTargetForReceiverClassId(kMint));
    if (target.IsNull()) {
      __ jmp(deopt);
    } else {
      __ pushl(left);
      __ pushl(right);
      compiler->GenerateStaticCall(comp->instance_call()->cid(),
                                   comp->instance_call()->token_pos(),
                                   comp->instance_call()->try_index(),
                                   target,
                                   comp->instance_call()->ArgumentCount(),
                                   comp->instance_call()->argument_names());
      ASSERT(result == EAX);
    }
  }
  __ Bind(&done);
}


void BinaryOpComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  switch (operands_type()) {
    case kSmiOperands:
      EmitSmiBinaryOp(compiler, this);
      break;

    case kMintOperands:
      EmitMintBinaryOp(compiler, this);
      break;

    default:
      UNREACHABLE();
  }
}


LocationSummary* DoubleBinaryOpComp::MakeLocationSummary() const {
  return MakeCallSummary();  // Calls into a stub for allocation.
}


void DoubleBinaryOpComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register left = EBX;
  Register right = ECX;
  Register temp = EDX;
  Register result = locs()->out().reg();

  const Class& double_class = compiler->double_class();
  const Code& stub =
    Code::Handle(StubCode::GetAllocationStubForClass(double_class));
  const ExternalLabel label(double_class.ToCString(), stub.EntryPoint());
  compiler->GenerateCall(instance_call()->token_pos(),
                         instance_call()->try_index(),
                         &label,
                         PcDescriptors::kOther);
  // Newly allocated object is now in the result register (RAX).
  ASSERT(result == EAX);
  __ popl(right);
  __ popl(left);

  Label* deopt = compiler->AddDeoptStub(instance_call()->cid(),
                                        instance_call()->token_pos(),
                                        instance_call()->try_index(),
                                        kDeoptDoubleBinaryOp,
                                        left,
                                        right);

  compiler->LoadDoubleOrSmiToXmm(XMM0, left, temp, deopt);
  compiler->LoadDoubleOrSmiToXmm(XMM1, right, temp, deopt);

  switch (op_kind()) {
    case Token::kADD: __ addsd(XMM0, XMM1); break;
    case Token::kSUB: __ subsd(XMM0, XMM1); break;
    case Token::kMUL: __ mulsd(XMM0, XMM1); break;
    case Token::kDIV: __ divsd(XMM0, XMM1); break;
    default: UNREACHABLE();
  }

  __ movsd(FieldAddress(result, Double::value_offset()), XMM0);
}


LocationSummary* UnarySmiOpComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  summary->set_out(Location::SameAsFirstInput());
  return summary;
}


void UnarySmiOpComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  const ICData& ic_data = *instance_call()->ic_data();
  ASSERT(!ic_data.IsNull());
  ASSERT(ic_data.num_args_tested() == 1);
  // TODO(srdjan): Implement for more checks.
  ASSERT(ic_data.NumberOfChecks() == 1);
  intptr_t test_class_id;
  Function& target = Function::Handle();
  ic_data.GetOneClassCheckAt(0, &test_class_id, &target);

  Register value = locs()->in(0).reg();
  Register result = locs()->out().reg();
  ASSERT(value == result);
  Label* deopt = compiler->AddDeoptStub(instance_call()->cid(),
                                        instance_call()->token_pos(),
                                        instance_call()->try_index(),
                                        kDeoptUnaryOp,
                                        value);
  if (test_class_id == kSmi) {
    __ testl(value, Immediate(kSmiTagMask));
    __ j(NOT_ZERO, deopt);
    switch (op_kind()) {
      case Token::kNEGATE:
        __ negl(value);
        __ j(OVERFLOW, deopt);
        break;
      case Token::kBIT_NOT:
        __ notl(value);
        __ andl(value, Immediate(~kSmiTagMask));  // Remove inverted smi-tag.
        break;
      default:
        UNREACHABLE();
    }
  } else {
    UNREACHABLE();
  }
}


LocationSummary* NumberNegateComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 1;  // Needed for doubles.
  LocationSummary* summary =
      new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
  summary->set_in(0, Location::RegisterLocation(EAX));
  summary->set_temp(0, Location::RegisterLocation(ECX));
  summary->set_out(Location::RegisterLocation(EAX));
  return summary;
}


void NumberNegateComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  const ICData& ic_data = *instance_call()->ic_data();
  ASSERT(!ic_data.IsNull());
  ASSERT(ic_data.num_args_tested() == 1);

  // TODO(srdjan): Implement for more checks.
  ASSERT(ic_data.NumberOfChecks() == 1);
  intptr_t test_class_id;
  Function& target = Function::Handle();
  ic_data.GetOneClassCheckAt(0, &test_class_id, &target);

  Register value = locs()->in(0).reg();
  Register result = locs()->out().reg();
  ASSERT(value == result);
  Label* deopt = compiler->AddDeoptStub(instance_call()->cid(),
                                        instance_call()->token_pos(),
                                        instance_call()->try_index(),
                                        kDeoptUnaryOp,
                                        value);
  if (test_class_id == kDouble) {
    Register temp = locs()->temp(0).reg();
    __ testl(value, Immediate(kSmiTagMask));
    __ j(ZERO, deopt);  // Smi.
    __ CompareClassId(value, kDouble, temp);
    __ j(NOT_EQUAL, deopt);
    // Allocate result object.
    const Class& double_class = compiler->double_class();
    const Code& stub =
        Code::Handle(StubCode::GetAllocationStubForClass(double_class));
    const ExternalLabel label(double_class.ToCString(), stub.EntryPoint());
    __ pushl(value);
    compiler->GenerateCall(instance_call()->token_pos(),
                           instance_call()->try_index(),
                           &label,
                           PcDescriptors::kOther);
    // Result is in EAX.
    ASSERT(result != temp);
    __ movl(result, EAX);
    __ popl(temp);
    __ movsd(XMM0, FieldAddress(temp, Double::value_offset()));
    __ DoubleNegate(XMM0);
    __ movsd(FieldAddress(result, Double::value_offset()), XMM0);
  } else {
    UNREACHABLE();
  }
}


LocationSummary* ToDoubleComp::MakeLocationSummary() const {
  const intptr_t kNumInputs = 1;
  if (from() == kDouble) {
    const intptr_t kNumTemps = 1;
    LocationSummary* locs =
        new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kNoCall);
    locs->set_in(0, Location::RequiresRegister());
    locs->set_temp(0, Location::RequiresRegister());
    locs->set_out(Location::SameAsFirstInput());
    locs->set_temp(0, Location::RequiresRegister());
    return locs;
  } else {
    ASSERT(from() == kSmi);
    return MakeCallSummary();  // Calls a stub to allocate result.
  }
}


void ToDoubleComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register value = (from() == kDouble) ? locs()->in(0).reg() : EBX;
  Register result = locs()->out().reg();

  const DeoptReasonId deopt_reason = (from() == kDouble) ?
      kDeoptDoubleToDouble : kDeoptIntegerToDouble;
  Label* deopt = compiler->AddDeoptStub(instance_call()->cid(),
                                        instance_call()->token_pos(),
                                        instance_call()->try_index(),
                                        deopt_reason,
                                        value);

  if (from() == kDouble) {
    Register temp = locs()->temp(0).reg();
    __ testl(value, Immediate(kSmiTagMask));
    __ j(ZERO, deopt);  // Deoptimize if Smi.
    __ CompareClassId(value, kDouble, temp);
    __ j(NOT_EQUAL, deopt);  // Deoptimize if not Double.
    ASSERT(value == result);
    return;
  }

  ASSERT(from() == kSmi);

  const Class& double_class = compiler->double_class();
  const Code& stub =
    Code::Handle(StubCode::GetAllocationStubForClass(double_class));
  const ExternalLabel label(double_class.ToCString(), stub.EntryPoint());

  // TODO(vegorov): allocate box in the driver loop to avoid spilling.
  compiler->GenerateCall(instance_call()->token_pos(),
                         instance_call()->try_index(),
                         &label,
                         PcDescriptors::kOther);
  ASSERT(result == EAX);
  __ popl(value);

  __ testl(value, Immediate(kSmiTagMask));
  __ j(NOT_ZERO, deopt);  // Deoptimize if not Smi.
  __ SmiUntag(value);
  __ cvtsi2sd(XMM0, value);
  __ movsd(FieldAddress(result, Double::value_offset()), XMM0);
}


LocationSummary* PolymorphicInstanceCallComp::MakeLocationSummary() const {
  return MakeCallSummary();
}


void PolymorphicInstanceCallComp::EmitNativeCode(FlowGraphCompiler* compiler) {
  Label* deopt = compiler->AddDeoptStub(instance_call()->cid(),
                                        instance_call()->token_pos(),
                                        instance_call()->try_index(),
                                        kDeoptPolymorphicInstanceCallTestFail);
  if (!HasICData() || (ic_data()->NumberOfChecks() == 0)) {
    __ jmp(deopt);
    return;
  }
  ASSERT(HasICData());
  ASSERT(ic_data()->num_args_tested() == 1);
  Label handle_smi;
  Label* is_smi_label =
      ic_data()->GetReceiverClassIdAt(0) == kSmi ?  &handle_smi : deopt;

  // Load receiver into EAX.
  __ movl(EAX,
      Address(ESP, (instance_call()->ArgumentCount() - 1) * kWordSize));
  __ testl(EAX, Immediate(kSmiTagMask));
  __ j(ZERO, is_smi_label);
  Label done;
  __ LoadClassId(EDI, EAX);
  compiler->EmitTestAndCall(*ic_data(),
                            EDI,  // Class id register.
                            instance_call()->ArgumentCount(),
                            instance_call()->argument_names(),
                            deopt,
                            (is_smi_label == &handle_smi) ? &done : NULL,
                            instance_call()->cid(),
                            instance_call()->token_pos(),
                            instance_call()->try_index());
  if (is_smi_label == &handle_smi) {
    __ Bind(&handle_smi);
    ASSERT(ic_data()->GetReceiverClassIdAt(0) == kSmi);
    const Function& target = Function::ZoneHandle(ic_data()->GetTargetAt(0));
    compiler->GenerateStaticCall(instance_call()->cid(),
                                 instance_call()->token_pos(),
                                 instance_call()->try_index(),
                                 target,
                                 instance_call()->ArgumentCount(),
                                 instance_call()->argument_names());
  }
  __ Bind(&done);
}


// TODO(srdjan): Move to shared.
static bool ICDataWithBothClassIds(const ICData& ic_data, intptr_t class_id) {
  if (ic_data.num_args_tested() != 2) return false;
  if (ic_data.NumberOfChecks() != 1) return false;
  Function& target = Function::Handle();
  GrowableArray<intptr_t> class_ids;
  ic_data.GetCheckAt(0, &class_ids, &target);
  return (class_ids[0] == class_id) && (class_ids[1] == class_id);
}


LocationSummary* BranchInstr::MakeLocationSummary() const {
  if ((kind() == Token::kEQ_STRICT) || (kind() == Token::kNE_STRICT)) {
    const int kNumInputs = 2;
    const int kNumTemps = 0;
    LocationSummary* locs =
        new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kNoCall);
    locs->set_in(0, Location::RequiresRegister());
    locs->set_in(1, Location::RequiresRegister());
    return locs;
  }
  if (HasICData() && (ic_data()->NumberOfChecks() > 0)) {
    if (ICDataWithBothClassIds(*ic_data(), kSmi) ||
        ICDataWithBothClassIds(*ic_data(), kDouble)) {
      const intptr_t kNumInputs = 2;
      const intptr_t kNumTemps = 1;
      LocationSummary* summary =
          new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kNoCall);
      summary->set_in(0, Location::RequiresRegister());
      summary->set_in(1, Location::RequiresRegister());
      summary->set_temp(0, Location::RequiresRegister());
      return summary;
    }
    if ((kind() == Token::kEQ) || (kind() == Token::kNE)) {
      const intptr_t kNumInputs = 2;
      const intptr_t kNumTemps = 1;
      LocationSummary* locs =
          new LocationSummary(kNumInputs, kNumTemps, LocationSummary::kCall);
      locs->set_in(0, Location::RegisterLocation(EAX));
      locs->set_in(1, Location::RegisterLocation(ECX));
      locs->set_temp(0, Location::RegisterLocation(EDX));
      return locs;
    }
    // Otherwise polymorphic dispatch.
  }
  // Call.
  return Computation::MakeCallSummary();
}


void BranchInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  if ((kind() == Token::kEQ_STRICT) || (kind() == Token::kNE_STRICT)) {
    Register left = locs()->in(0).reg();
    Register right = locs()->in(1).reg();
    __ cmpl(left, right);
    Condition cond = (kind() == Token::kEQ_STRICT) ? EQUAL : NOT_EQUAL;
    EmitBranchOnCondition(compiler, cond);
    return;
  }
  // Relational or equality.
  if (HasICData() && (ic_data()->NumberOfChecks() > 0)) {
    if (ICDataWithBothClassIds(*ic_data(), kSmi)) {
      EmitSmiComparisonOp(compiler, *locs(), kind(), this,
                          cid(), token_pos(), try_index());
      return;
    }
    if (ICDataWithBothClassIds(*ic_data(), kDouble)) {
      EmitDoubleComparisonOp(compiler, *locs(), kind(), this,
                             cid(), token_pos(), try_index());
      return;
    }
    // TODO(srdjan): Add Smi/Double, Double/Smi comparisons.
    if ((kind() == Token::kEQ) || (kind() == Token::kNE)) {
      EmitGenericEqualityCompare(compiler, *locs(), kind(), this, *ic_data(),
                                 cid(), token_pos(), try_index());
      return;
    }
    // Otherwise polymorphic dispatch?
  }
  // Not equal is always split into '==' and negate,
  Condition branch_condition = (kind() == Token::kNE) ? NOT_EQUAL : EQUAL;
  Token::Kind call_kind = (kind() == Token::kNE) ? Token::kEQ : kind();
  const String& function_name =
      String::ZoneHandle(Symbols::New(Token::Str(call_kind)));
  compiler->AddCurrentDescriptor(PcDescriptors::kDeopt,
                                 cid(),
                                 token_pos(),
                                 try_index());
  const intptr_t kNumArguments = 2;
  const intptr_t kNumArgsChecked = 2;  // Type-feedback.
  compiler->GenerateInstanceCall(cid(),
                                 token_pos(),
                                 try_index(),
                                 function_name,
                                 kNumArguments,
                                 Array::ZoneHandle(),  // No optional arguments.
                                 kNumArgsChecked);
  ASSERT(locs()->out().reg() == EAX);
  __ CompareObject(locs()->out().reg(), compiler->bool_true());
  EmitBranchOnCondition(compiler, branch_condition);
}

}  // namespace dart

#undef __

#endif  // defined TARGET_ARCH_X64
