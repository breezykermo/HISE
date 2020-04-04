/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   Commercial licences for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licencing:
*
*   http://www.hartinstruments.net/hise/
*
*   HISE is based on the JUCE library,
*   which also must be licenced for commercial applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

#pragma once

namespace snex {
namespace jit {
using namespace juce;
using namespace asmjit;


snex::jit::Symbol Symbol::getParentSymbol(NamespaceHandler* handler) const
{
	auto p = id.getParent();

	if (p.isValid())
	{
		auto t = handler->getVariableType(id);
		return Symbol(p, t);
	}
	else
		return Symbol(Identifier());
}

snex::jit::Symbol Symbol::getChildSymbol(const Identifier& childName, NamespaceHandler* handler) const
{
	auto cId = id.getChildId(childName);
	auto t = handler->getVariableType(cId);
	return Symbol(cId, t);
}

Symbol::operator bool() const
{
	return !id.isNull() && id.isValid();
}

juce::String FunctionData::getSignature(const Array<Identifier>& parameterIds) const
{
	juce::String s;

	s << returnType.toString() << " " << id.toString();
	
	if (!templateParameters.isEmpty())
	{
		s << "<";

		for(int i = 0; i < templateParameters.size(); i++)
		{
			auto t = templateParameters[i];

			if (t.type.isValid())
				s << t.type.toString();
			else
				s << juce::String(t.constant);

			if (i == (templateParameters.size() - 1))
				s << ">";
			else
				s << ", ";
		}
	}
	
	s << "(";

	int index = 0;

	for (auto arg : args)
	{
		s << arg.typeInfo.toString();

		auto pName = parameterIds[index].toString();

		if (pName.isEmpty())
			pName = arg.id.toString();

		if (pName.isNotEmpty())
			s << " " << pName;

		if (++index != args.size())
			s << ", ";
	}

	s << ")";

	return s;
}

bool FunctionData::matchIdArgs(const FunctionData& other) const
{
	auto idMatch = id == other.id;
	auto argMatch = matchesArgumentTypes(other);
	return idMatch && argMatch;
}

bool FunctionData::matchIdArgsAndTemplate(const FunctionData& other) const
{
	auto idArgMatch = matchIdArgs(other);
	auto templateMatch = matchesTemplateArguments(other.templateParameters);

	return idArgMatch && templateMatch;
}

bool FunctionData::matchesArgumentTypes(const Array<TypeInfo>& typeList) const
{
	if (args.size() != typeList.size())
		return false;

	for (int i = 0; i < args.size(); i++)
	{
		auto thisArgs = args[i].typeInfo;
		auto otherArgs = typeList[i];

		if (thisArgs.isInvalid())
			continue;

		if (otherArgs.getType() == thisArgs.getType())
			continue;

		if (thisArgs != otherArgs)
		{
			return false;
		}
			
	}

	return true;
}

bool FunctionData::matchesArgumentTypes(TypeInfo r, const Array<TypeInfo>& argsList) const
{
	if (r != returnType)
		return false;

	return matchesArgumentTypes(argsList);
}

bool FunctionData::matchesArgumentTypes(const FunctionData& otherFunctionData, bool checkReturnType /*= true*/) const
{
	if (checkReturnType && otherFunctionData.returnType != returnType)
		return false;

	if (args.size() != otherFunctionData.args.size())
		return false;

	for (int i = 0; i < args.size(); i++)
	{
		auto thisType = args[i].typeInfo;
		auto otherType = otherFunctionData.args[i].typeInfo;

		if (thisType != otherType)
			return false;
	}

	return true;
}




bool FunctionData::matchesNativeArgumentTypes(Types::ID r, const Array<Types::ID>& nativeArgList) const
{
	Array<TypeInfo> t;

	for (auto n : nativeArgList)
		t.add(TypeInfo(n));

	return matchesArgumentTypes(TypeInfo(r), t);
}



bool FunctionData::matchesTemplateArguments(const TemplateParameter::List& l) const
{
	if (l.size() != templateParameters.size())
		return false;

	for (int i = 0; i < l.size(); i++)
	{
		if (l[i] != templateParameters[i])
			return false;
	}

	return true;
}

struct SyntaxTreeInlineData: public InlineData
{
	SyntaxTreeInlineData(Operations::Statement::Ptr e_, const NamespacedIdentifier& path_):
		expression(e_),
		location(e_->location),
		path(path_)
	{

	}

	bool isHighlevel() const override
	{
		return true;
	}

	bool replaceIfSuccess()
	{
		if (target != nullptr)
		{
			expression->replaceInParent(target);

			auto c = expression->currentCompiler;
			auto s = expression->currentScope;

			if (auto t = dynamic_cast<Operations::StatementBlock*>(target.get()))
				s = t->createOrGetBlockScope(s);

			for (int i = 0; i <= expression->currentPass; i++)
			{
				auto thisPass = (BaseCompiler::Pass)i;

				BaseCompiler::ScopedPassSwitcher svs(c, thisPass);

				c->executePass(thisPass, s, target.get());
			};

			jassert(target->currentPass == expression->currentPass);

			return true;
		}
			
		return false;
	}

	ParserHelpers::CodeLocation location;
	Operations::Statement::Ptr expression;
	Operations::Statement::Ptr target;
	Operations::Statement::Ptr object;
	ReferenceCountedArray<Operations::Expression> args;
	NamespacedIdentifier path;
};

struct AsmInlineData: public InlineData
{
	AsmInlineData(AsmCodeGenerator& gen_) :
		gen(gen_)
	{};

	bool isHighlevel() const override
	{
		return false;
	}

	AsmCodeGenerator& gen;
	AssemblyRegister::Ptr target;
	AssemblyRegister::Ptr object;
	AssemblyRegister::List args;
};

struct ReturnTypeInlineData : public InlineData
{
	ReturnTypeInlineData(FunctionData& f_) :
		f(f_)
	{
		templateParameters = f.templateParameters;
	};

	bool isHighlevel() const override { return true; }

	Operations::Expression::Ptr object;
	FunctionData& f;
};


void ComplexType::registerExternalAtNamespaceHandler(NamespaceHandler* handler)
{
	if (hasAlias())
	{
		jassert(getAlias().isExplicit());
		handler->addSymbol(getAlias(), TypeInfo(this), NamespaceHandler::UsingAlias);
	}
}

bool ComplexType::isValidCastSource(Types::ID nativeSourceType, ComplexType::Ptr complexSourceType) const
{
	if (complexSourceType == this)
		return true;

	return false;
}

bool ComplexType::isValidCastTarget(Types::ID nativeTargetType, ComplexType::Ptr complexTargetType) const
{
	if (complexTargetType == this)
		return true;

	return false;
}


bool FunctionClass::hasFunction(const NamespacedIdentifier& s) const
{
	if (getClassName() == s)
		return true;

	auto parent = s.getParent();

	if (parent == classSymbol)
	{
		for (auto f : functions)
			if (f->id == s)
				return true;
	}

	for (auto c : registeredClasses)
	{
		if (c->hasFunction(s))
			return true;
	}

	return false;
}



bool FunctionClass::hasConstant(const NamespacedIdentifier& s) const
{
	auto parent = s.getParent();

	if (parent == classSymbol)
	{
		for (auto& c : constants)
			if (c.id == s.getIdentifier())
				return true;
	}

	for (auto c : registeredClasses)
	{
		if (c->hasConstant(s))
			return true;
	}

	return false;
}

void FunctionClass::addFunctionConstant(const Identifier& constantId, VariableStorage value)
{
	constants.add({ constantId, value });
}

void FunctionClass::addMatchingFunctions(Array<FunctionData>& matches, const NamespacedIdentifier& symbol) const
{
	auto parent = symbol.getParent();

	if (parent == classSymbol)
	{
		for (auto f : functions)
		{
			if (f->id == symbol)
				matches.add(*f);
		}
	}
	else
	{
		for (auto c : registeredClasses)
			c->addMatchingFunctions(matches, symbol);
	}
}


void FunctionClass::addFunctionClass(FunctionClass* newRegisteredClass)
{
	registeredClasses.add(newRegisteredClass);
}


void FunctionClass::addFunction(FunctionData* newData)
{
	if (newData->id.isExplicit())
	{
		newData->id = getClassName().getChildId(newData->id.getIdentifier());
	}

	functions.add(newData);
}


Array<NamespacedIdentifier> FunctionClass::getFunctionIds() const
{
	Array<NamespacedIdentifier> ids;

	for (auto r : functions)
		ids.add(r->id);

	return ids;
}

bool FunctionClass::fillJitFunctionPointer(FunctionData& dataWithoutPointer)
{
	// first check strict typing
	for (auto f : functions)
	{
		if (f->matchIdArgsAndTemplate(dataWithoutPointer))
		{
			dataWithoutPointer.function = f->function;
			return dataWithoutPointer.function != nullptr;
		}
	}

	for (auto f : functions)
	{
		bool idMatch = f->id == dataWithoutPointer.id;
		auto templateMatch = f->matchesTemplateArguments(dataWithoutPointer.templateParameters);

		if (idMatch && templateMatch)
		{
			auto& fArgs = f->args;
			auto& dArgs = dataWithoutPointer.args;

			if (fArgs.size() == dArgs.size())
			{
				

				dataWithoutPointer.function = f->function;
				return true;
			}
		}
	}

	return false;
}


bool FunctionClass::injectFunctionPointer(FunctionData& dataToInject)
{
	for (auto f : functions)
	{
		if (f->matchIdArgsAndTemplate(dataToInject))
		{
			f->function = dataToInject.function;
			return true;
		}
	}

	return false;
}

FunctionData FunctionClass::getSpecialFunction(SpecialSymbols s, TypeInfo returnType, const TypeInfo::List& argTypes) const
{
	if (hasSpecialFunction(s))
	{
		Array<FunctionData> matches;

		addSpecialFunctions(s, matches);

		if (returnType.isInvalid() && argTypes.isEmpty())
		{
			if (matches.size() == 1)
				return matches.getFirst();
		}

		for (auto& m : matches)
		{
			if (m.matchesArgumentTypes(returnType, argTypes))
				return m;
		}
	}

	return {};
}

snex::VariableStorage FunctionClass::getConstantValue(const NamespacedIdentifier& s) const
{
	auto parent = s.getParent();

	if (parent == classSymbol)
	{
		for (auto& c : constants)
		{
			if (c.id == s.getIdentifier())
				return c.value;
		}
	}

	for (auto r : registeredClasses)
	{
		auto v = r->getConstantValue(s);

		if (!v.isVoid())
			return v;
	}

	return {};
}

int ComplexType::numInstances = 0;

void ComplexType::writeNativeMemberTypeToAsmStack(const ComplexType::InitData& d, int initIndex, int offsetInBytes, int size)
{
	auto& cc = GET_COMPILER_FROM_INIT_DATA(d);
	auto mem = d.asmPtr->memory.cloneAdjustedAndResized(offsetInBytes, size);

	if (auto expr = dynamic_cast<Operations::Expression*>(d.initValues->getExpression(initIndex)))
	{
		auto source = expr->reg;
		source->loadMemoryIntoRegister(cc);
		auto type = source->getType();

		IF_(int) cc.mov(mem, INT_REG_R(source));
		IF_(double) cc.movsd(mem, FP_REG_R(source));
		IF_(float) cc.movss(mem, FP_REG_R(source));
		IF_(void*) cc.mov(mem, INT_REG_R(source));
	}
	else
	{
		VariableStorage v;
		d.initValues->getValue(initIndex, v);

		auto type = v.getType();
		
		IF_(int)
		{
			cc.mov(mem, v.toInt());
		}
		IF_(float)
		{
			auto t = cc.newFloatConst(ConstPool::kScopeLocal, v.toFloat());
			auto temp = cc.newXmmPs();
			cc.movss(temp, t);
			cc.movss(mem, temp);
		}
		IF_(double)
		{
			auto t = cc.newDoubleConst(ConstPool::kScopeLocal, v.toDouble());
			auto temp = cc.newXmmPd();
			cc.movsd(temp, t);
			cc.movsd(mem, temp);
		}
	}
}

SyntaxTreeInlineData* InlineData::toSyntaxTreeData() const
{
	jassert(isHighlevel());
	return dynamic_cast<SyntaxTreeInlineData*>(const_cast<InlineData*>(this));
}

AsmInlineData* InlineData::toAsmInlineData() const
{
	jassert(!isHighlevel());

	return dynamic_cast<AsmInlineData*>(const_cast<InlineData*>(this));
}

juce::Result Inliner::process(InlineData* d) const
{
	if (dynamic_cast<ReturnTypeInlineData*>(d))
	{
		return returnTypeFunction(d);
	}

	if (d->isHighlevel() && highLevelFunc)
		return highLevelFunc(d);

	if (!d->isHighlevel() && asmFunc)
		return asmFunc(d);

	return Result::fail("Can't inline function");
}

bool TemplateParameter::ListOps::isParameter(const TemplateParameter::List& l)
{
	for (const auto& p : l)
	{
		if (!p.isTemplateArgument())
			return true;
	}

	return false;
}

bool TemplateParameter::ListOps::isArgument(const TemplateParameter::List& l)
{
	for (const auto& p : l)
	{
		if (p.isTemplateArgument())
		{
			jassert(!isParameter(l));
			return true;
		}
	}

	return false;
}

bool TemplateParameter::ListOps::isArgumentOrEmpty(const List& l)
{
	if (l.isEmpty())
		return true;

	return isArgument(l);
}

bool TemplateParameter::ListOps::match(const List& first, const List& second)
{
	if (first.size() != second.size())
		return false;

	for (int i = 0; i < first.size(); i++)
	{
		auto f = first[i];
		auto s = second[i];

		if (f != s)
			return false;
	}

	return true;
}

bool TemplateParameter::ListOps::isNamed(const List& l)
{
	for (auto& p : l)
	{
		if (!p.argumentId.isValid())
			return false;
	}

	return true;
}

bool TemplateParameter::ListOps::readyToResolve(const List& l)
{
	return isNamed(l) && isParameter(l);
}

juce::String TemplateParameter::ListOps::toString(const List& l)
{
	if (l.isEmpty())
		return {};

	juce::String s;

	s << "<";

	for (int i = 0; i < l.size(); i++)
	{
		auto& t = l[i];

		if (t.isTemplateArgument())
		{
			if (t.t == TypeTemplateArgument)
			{
				s << "typename " << t.argumentId.getIdentifier();

				if (t.type.isValid())
					s << "=" << t.type.toString();
			}
			else
			{
				s << "int " << t.argumentId.getIdentifier();

				if (t.constant != 0)
					s << "=" << juce::String(t.constant);
			}
			
		}
		else
		{
			if (t.type.isValid())
				s << t.type.toString();
			else
				s << juce::String(t.constant);
		}

		

		if (i != l.size()-1)
			s << ", ";
	}

	s << ">";

	return s;
}

TemplateParameter::List TemplateParameter::ListOps::filter(const List& l, const NamespacedIdentifier& id)
{
	List r;

	for (auto& p : l)
	{
		if (p.argumentId.getParent() == id)
			r.add(p);
	}

	return r;
}

TemplateParameter::List TemplateParameter::ListOps::merge(const TemplateParameter::List& arguments, const TemplateParameter::List& parameters, juce::Result& r)
{
	if (arguments.isEmpty() && parameters.isEmpty())
		return parameters;

	for (auto& a : arguments)
	{
		// The argument array must contain Template arguments only...
		jassert(a.isTemplateArgument());
	}

	for (auto& p : parameters)
	{
		// the parameter array must contain template parameters only
		// (ParameterType::Type or ParameterType::Constant)
		jassert(!p.isTemplateArgument());
	}

	TemplateParameter::List instanceParameters;

	auto numArgs = arguments.size();
	auto numDefinedParameters = parameters.size();

	if (numDefinedParameters > numArgs)
	{
		r = Result::fail("Too many template parameters");
		return instanceParameters;
	}

	for (int i = 0; i < numArgs; i++)
	{
		if (isPositiveAndBelow(i, numDefinedParameters))
		{
			TemplateParameter p = parameters[i];
			p.argumentId = arguments[i].argumentId;
			instanceParameters.add(p);
		}
		else
		{
			TemplateParameter p = arguments[i];
			jassert(p.argumentId.isValid());

			if (p.t == TemplateParameter::TypeTemplateArgument)
			{
				jassert(p.type.isValid());
				p.t = TemplateParameter::ParameterType::Type;
			}
			else
				p.t = TemplateParameter::ParameterType::ConstantInteger;

			instanceParameters.add(p);
		}
	}

	for (auto& p : instanceParameters)
	{
		if (!p.isResolved())
		{
			r = Result::fail("Missing template specialisation for " + p.argumentId.toString());
		}
	}

	return instanceParameters;
}

TemplateParameter::List TemplateParameter::ListOps::sort(const List& arguments, const List& parameters, juce::Result& r)
{
	jassert(isArgumentOrEmpty(arguments));
	jassert(isParameter(parameters) || parameters.isEmpty());

	if (arguments.size() != parameters.size())
		return parameters;

	for (auto& p : parameters)
	{
		if (!p.argumentId.isValid())
			return parameters;
	}

	TemplateParameter::List tp;

	for (int i = 0; i < arguments.size(); i++)
	{
		for (int j = 0; j < parameters.size(); j++)
		{
			if (arguments[i].argumentId == parameters[j].argumentId)
			{
				tp.add(parameters[j]);
				break;
			}
		}
	}

	return tp;
}

TemplateParameter::List TemplateParameter::ListOps::mergeWithCallParameters(const List& argumentList, const List& existing, const TypeInfo::List& originalFunctionArguments, const TypeInfo::List& callParameterTypes, Result& r)
{
	jassert(existing.isEmpty() || isParameter(existing));

	List tp = existing;

	jassert(callParameterTypes.size() == originalFunctionArguments.size());

	for (int i = 0; i < originalFunctionArguments.size(); i++)
	{
		auto& o = originalFunctionArguments[i];
		auto &cp = callParameterTypes[i];

		if (o.isTemplateType())
		{
			// Check if the type is directly used...
			auto typeTouse = cp.withModifiers(o.isConst(), o.isRef());
			TemplateParameter tId(typeTouse);
			tId.argumentId = o.getTemplateId();

			for (auto& existing : tp)
			{
				if (existing.argumentId == tId.argumentId)
				{
					if (existing != tId)
					{
						r = Result::fail("Can't deduce template type from arguments");
						return {};
					}
				}
			}

			tp.addIfNotAlreadyThere(tId);
		}
		else if (auto ctd = o.getTypedIfComplexType<TemplatedComplexType>())
		{
			// check if the type can be deducted by the template parameters...

			auto pt = cp.getTypedIfComplexType<ComplexTypeWithTemplateParameters>();

			jassert(pt != nullptr);

			auto fArgTemplates = ctd->getTemplateInstanceParameters();
			auto fParTemplates = pt->getTemplateInstanceParameters();

			jassert(fArgTemplates.size() == fParTemplates.size());

			for (int i = 0; i < fArgTemplates.size(); i++)
			{
				auto& fa = fArgTemplates[i];
				auto& fp = fParTemplates[i];

				if (fa.type.isTemplateType())
				{
					auto fpId = fa.type.getTemplateId();

					for (auto& a : argumentList)
					{
						if (a.argumentId == fpId)
						{
							TemplateParameter tId = fp;
							tId.argumentId = fpId;

							tp.addIfNotAlreadyThere(tId);
						}
					}
				}
			}
		}
	}

	return sort(argumentList, tp, r);
}

snex::jit::ComplexType::Ptr TemplatedComplexType::createTemplatedInstance(const TemplateParameter::List& suppliedTemplateParameters, juce::Result& r)
{
	TemplateParameter::List instanceParameters;

	for (const auto& p : d.tp)
	{
		if (p.type.isTemplateType())
		{
			for (const auto& sp : suppliedTemplateParameters)
			{
				if (sp.argumentId == p.type.getTemplateId())
				{
					if (sp.t == TemplateParameter::ConstantInteger)
					{
						TemplateParameter ip(sp.constant);
						ip.argumentId = sp.argumentId;
						instanceParameters.add(ip);
					}
					else
					{
						TemplateParameter ip(sp.type);
						ip.argumentId = sp.argumentId;
						instanceParameters.add(ip);
					}
				}
			}
		}
		else if (p.isTemplateArgument())
		{
			for (const auto& sp : suppliedTemplateParameters)
			{
				if (sp.argumentId == p.argumentId)
				{
					jassert(sp.isResolved());
					TemplateParameter ip = sp;
					instanceParameters.add(ip);
				}
			}
		}
		else
		{
			jassert(p.isResolved());
			instanceParameters.add(p);
		}
	}

	for (auto& p : instanceParameters)
	{
		jassert(p.isResolved());
	}

	TemplateObject::ConstructData instanceData = d;
	instanceData.tp = instanceParameters;

	instanceData.r = &r;

	ComplexType::Ptr p = c.makeClassType(instanceData);

	p = instanceData.handler->registerComplexTypeOrReturnExisting(p);

	return p;
}

snex::jit::ComplexType::Ptr TemplatedComplexType::createSubType(SubTypeConstructData* sd)
{


	auto id = sd->id;
	auto sl = sd->l;

	ComplexType::Ptr parentType = this;

	TemplateObject s;
	s.id = id.relocate(id.getParent(), c.id);
	s.makeClassType = [parentType, id, sl](const TemplateObject::ConstructData& sc)
	{
		auto parent = dynamic_cast<TemplatedComplexType*>(parentType.get());

		auto parentType = parent->createTemplatedInstance(sc.tp, *sc.r);

		if (!sc.r->wasOk())
			return parentType;

		parentType = sc.handler->registerComplexTypeOrReturnExisting(parentType);

		SubTypeConstructData nsd;
		nsd.id = id;
		nsd.l = sl;
		nsd.handler = sc.handler;

		auto childType = parentType->createSubType(&nsd);

		return childType;
	};

	return new TemplatedComplexType(s, d);
}

} // end namespace jit
} // end namespace snex
