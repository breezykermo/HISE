

//==============================================================================
struct HiseJavascriptEngine::RootObject::TokenIterator
{
	TokenIterator(const String& code, const String &externalFile) : location(code, externalFile), p(code.getCharPointer()) { skip(); }

	void skip()
	{
		skipWhitespaceAndComments();
		location.location = p;
		currentType = matchNextToken();
	}

	void match(TokenType expected)
	{
		if (currentType != expected)
			location.throwError("Found " + getTokenName(currentType) + " when expecting " + getTokenName(expected));

		skip();
	}

	bool matchIf(TokenType expected)                                 { if (currentType == expected)  { skip(); return true; } return false; }
	bool matchesAny(TokenType t1, TokenType t2) const                { return currentType == t1 || currentType == t2; }
	bool matchesAny(TokenType t1, TokenType t2, TokenType t3) const  { return matchesAny(t1, t2) || currentType == t3; }

	CodeLocation location;
	TokenType currentType;
	var currentValue;

	void clearLastComment()
	{
		lastComment = String();
	}

	String lastComment;

private:
	String::CharPointerType p;

	static bool isIdentifierStart(const juce_wchar c) noexcept{ return CharacterFunctions::isLetter(c) || c == '_'; }
	static bool isIdentifierBody(const juce_wchar c) noexcept{ return CharacterFunctions::isLetterOrDigit(c) || c == '_'; }

		TokenType matchNextToken()
	{
		if (isIdentifierStart(*p))
		{
			String::CharPointerType end(p);
			while (isIdentifierBody(*++end)) {}

			const size_t len = (size_t)(end - p);
#define JUCE_JS_COMPARE_KEYWORD(name, str) if (len == sizeof (str) - 1 && matchToken (TokenTypes::name, len)) return TokenTypes::name;
			JUCE_JS_KEYWORDS(JUCE_JS_COMPARE_KEYWORD)

				currentValue = String(p, end); p = end;
			return TokenTypes::identifier;
		}

		if (p.isDigit())
		{
			if (parseHexLiteral() || parseFloatLiteral() || parseOctalLiteral() || parseDecimalLiteral())
				return TokenTypes::literal;

			location.throwError("Syntax error in numeric constant");
		}

		if (parseStringLiteral(*p) || (*p == '.' && parseFloatLiteral()))
			return TokenTypes::literal;

#define JUCE_JS_COMPARE_OPERATOR(name, str) if (matchToken (TokenTypes::name, sizeof (str) - 1)) return TokenTypes::name;
		JUCE_JS_OPERATORS(JUCE_JS_COMPARE_OPERATOR)

			if (!p.isEmpty())
				location.throwError("Unexpected character '" + String::charToString(*p) + "' in source");

		return TokenTypes::eof;
	}

	bool matchToken(TokenType name, const size_t len) noexcept
	{
		if (p.compareUpTo(CharPointer_ASCII(name), (int)len) != 0) return false;
		p += (int)len;  return true;
	}


	void skipWhitespaceAndComments()
	{
		for (;;)
		{
			p = p.findEndOfWhitespace();

			if (*p == '/')
			{
				const juce_wchar c2 = p[1];

				if (c2 == '/')  { p = CharacterFunctions::find(p, (juce_wchar) '\n'); continue; }

				if (c2 == '*')
				{
					location.location = p;

					lastComment = String(p).upToFirstOccurrenceOf("*/", false, false).fromFirstOccurrenceOf("/**", false, false).trim();
					
					p = CharacterFunctions::find(p + 2, CharPointer_ASCII("*/"));

					
					if (p.isEmpty()) location.throwError("Unterminated '/*' comment");
					p += 2; continue;
				}
			}

			break;
		}
	}

	bool parseStringLiteral(juce_wchar quoteType)
	{
		if (quoteType != '"' && quoteType != '\'')
			return false;

		Result r(JSON::parseQuotedString(p, currentValue));
		if (r.failed()) location.throwError(r.getErrorMessage());
		return true;
	}

	bool parseHexLiteral()
	{
		if (*p != '0' || (p[1] != 'x' && p[1] != 'X')) return false;

		String::CharPointerType t(++p);
		int64 v = CharacterFunctions::getHexDigitValue(*++t);
		if (v < 0) return false;

		for (;;)
		{
			const int digit = CharacterFunctions::getHexDigitValue(*++t);
			if (digit < 0) break;
			v = v * 16 + digit;
		}

		currentValue = v; p = t;
		return true;
	}

	bool parseFloatLiteral()
	{
		int numDigits = 0;
		String::CharPointerType t(p);
		while (t.isDigit())  { ++t; ++numDigits; }

		const bool hasPoint = (*t == '.');

		if (hasPoint)
			while ((++t).isDigit())  ++numDigits;

		if (numDigits == 0)
			return false;

		juce_wchar c = *t;
		const bool hasExponent = (c == 'e' || c == 'E');

		if (hasExponent)
		{
			c = *++t;
			if (c == '+' || c == '-')  ++t;
			if (!t.isDigit()) return false;
			while ((++t).isDigit()) {}
		}

		if (!(hasExponent || hasPoint)) return false;

		currentValue = CharacterFunctions::getDoubleValue(p);  p = t;
		return true;
	}

	bool parseOctalLiteral()
	{
		String::CharPointerType t(p);
		int64 v = *t - '0';
		if (v != 0) return false;  // first digit of octal must be 0

		for (;;)
		{
			const int digit = (int)(*++t - '0');
			if (isPositiveAndBelow(digit, 8))        v = v * 8 + digit;
			else if (isPositiveAndBelow(digit, 10))  location.throwError("Decimal digit in octal constant");
			else break;
		}

		currentValue = v;  p = t;
		return true;
	}

	bool parseDecimalLiteral()
	{
		int64 v = 0;

		for (;; ++p)
		{
			const int digit = (int)(*p - '0');
			if (isPositiveAndBelow(digit, 10))  v = v * 10 + digit;
			else break;
		}

		currentValue = v;
		return true;
	}
};

//==============================================================================
struct HiseJavascriptEngine::RootObject::ExpressionTreeBuilder : private TokenIterator
{
	ExpressionTreeBuilder(const String code, const String externalFile) :
		TokenIterator(code, externalFile){}

	void setupApiData(HiseSpecialData &data, const String& codeToPreprocess)
	{
		hiseSpecialData = &data;
		currentNamespace = hiseSpecialData;
		
		preprocessCode(codeToPreprocess);
	}

	void preprocessCode(const String& codeToPreprocess, const String& externalFileName="");

	BlockStatement* parseStatementList()
	{
		ScopedPointer<BlockStatement> b(new BlockStatement(location));

		while (currentType != TokenTypes::closeBrace && currentType != TokenTypes::eof)
		{
			ScopedPointer<Statement> s = parseStatement();

			if (LockStatement* ls = dynamic_cast<LockStatement*>(s.get()))
			{
				b->lockStatements.add(ls);
				s.release();
			}
			else
			{
				b->statements.add(s.release());
			}
		}

		return b.release();
	}

	void parseFunctionParamsAndBody(FunctionObject& fo)
	{
		match(TokenTypes::openParen);

		while (currentType != TokenTypes::closeParen)
		{
			fo.parameters.add(currentValue.toString());
			match(TokenTypes::identifier);

			if (currentType != TokenTypes::closeParen)
				match(TokenTypes::comma);
		}

		match(TokenTypes::closeParen);
		fo.body = parseBlock();
	}

	Expression* parseExpression()
	{
		Identifier id = Identifier::isValidIdentifier(currentValue.toString()) ? Identifier(currentValue.toString()) : Identifier::null;

		ExpPtr lhs(parseLogicOperator());

		if (matchIf(TokenTypes::in))
		{
			ExpPtr rhs(parseExpression());

			currentIterator = id;

			return rhs.release();
		}

		if (matchIf(TokenTypes::question))          return parseTerneryOperator(lhs);
		if (matchIf(TokenTypes::assign))            { ExpPtr rhs(parseExpression()); return new Assignment(location, lhs, rhs); }
		if (matchIf(TokenTypes::plusEquals))        return parseInPlaceOpExpression<AdditionOp>(lhs);
		if (matchIf(TokenTypes::minusEquals))       return parseInPlaceOpExpression<SubtractionOp>(lhs);
		if (matchIf(TokenTypes::leftShiftEquals))   return parseInPlaceOpExpression<LeftShiftOp>(lhs);
		if (matchIf(TokenTypes::rightShiftEquals))  return parseInPlaceOpExpression<RightShiftOp>(lhs);

		return lhs.release();
	}

	

private:

	

	HiseSpecialData *hiseSpecialData;

	bool currentlyParsingInlineFunction = false;
	Identifier currentlyParsedCallback = Identifier::null;

	JavascriptNamespace* currentNamespace = nullptr;

	JavascriptNamespace* getCurrentNamespace()
	{
		jassert(currentNamespace != nullptr);

		return currentNamespace;
	}

	void throwError(const String& err) const  { location.throwError(err); }

	template <typename OpType>
	Expression* parseInPlaceOpExpression(ExpPtr& lhs)
	{
		ExpPtr rhs(parseExpression());
		Expression* bareLHS = lhs; // careful - bare pointer is deliberately alised
		return new SelfAssignment(location, bareLHS, new OpType(location, lhs, rhs));
	}

	BlockStatement* parseBlock()
	{
		match(TokenTypes::openBrace);
		ScopedPointer<BlockStatement> b(parseStatementList());
		match(TokenTypes::closeBrace);
		return b.release();
	}

	Statement* parseStatement()
	{
		if (matchIf(TokenTypes::include_))		   return parseExternalFile();
		if (matchIf(TokenTypes::inline_))		   return parseInlineFunction(getCurrentNamespace());

		if (currentType == TokenTypes::openBrace)   return parseBlock();

		if (matchIf(TokenTypes::const_))		   return parseConstVar(getCurrentNamespace());
		if (matchIf(TokenTypes::var))              return parseVar();
		if (matchIf(TokenTypes::register_var))	   return parseRegisterVar(getCurrentNamespace());
		if (matchIf(TokenTypes::global_))		   return parseGlobalAssignment();
		if (matchIf(TokenTypes::local_))		   return parseLocalAssignment();
		if (matchIf(TokenTypes::namespace_))	   return parseNamespace();
		if (matchIf(TokenTypes::if_))              return parseIf();
		if (matchIf(TokenTypes::while_))           return parseDoOrWhileLoop(false);
		if (matchIf(TokenTypes::do_))              return parseDoOrWhileLoop(true);
		if (matchIf(TokenTypes::for_))             return parseForLoop();
		if (matchIf(TokenTypes::return_))          return parseReturn();
		if (matchIf(TokenTypes::switch_))		   return parseSwitchBlock();
		if (matchIf(TokenTypes::break_))           return new BreakStatement(location);
		if (matchIf(TokenTypes::continue_))        return new ContinueStatement(location);
		if (matchIf(TokenTypes::function))         return parseFunction();
		if (matchIf(TokenTypes::extern_))		   return parseExternalCFunction();
		if (matchIf(TokenTypes::semicolon))        return new Statement(location);
		if (matchIf(TokenTypes::plusplus))         return parsePreIncDec<AdditionOp>();
		if (matchIf(TokenTypes::minusminus))       return parsePreIncDec<SubtractionOp>();
		if (matchIf(TokenTypes::rLock_))		   return parseLockStatement(true);
		if (matchIf(TokenTypes::wLock_))		   return parseLockStatement(false);

		if (matchesAny(TokenTypes::openParen, TokenTypes::openBracket))
			return matchEndOfStatement(parseFactor());

		if (matchesAny(TokenTypes::identifier, TokenTypes::literal, TokenTypes::minus))
		{
			ExpPtr ex = parseExpression();
			return matchEndOfStatement(ex.release());
		}

		throwError("Found " + getTokenName(currentType) + " when expecting a statement");
		return nullptr;
	}

	String getFileContent(const String &fileNameInScript, String &refFileName)
	{

#if USE_BACKEND

		const String fileName = "{PROJECT_FOLDER}" + fileNameInScript.removeCharacters("\"\'");
		refFileName = GET_PROJECT_HANDLER(dynamic_cast<Processor*>(hiseSpecialData->processor)).getFilePath(fileName, ProjectHandler::SubDirectories::Scripts);

		File f(refFileName);

		const String shortFileName = f.getFileName();

		if (!f.existsAsFile())
			throwError("File " + refFileName + " not found");

		for (int i = 0; i < hiseSpecialData->includedFiles.size(); i++)
		{
			if (hiseSpecialData->includedFiles[i]->f == f)
				throwError("File " + shortFileName + " was included multiple times");
		}

		String fileContent = f.loadFileAsString();

#else
		//const String fileName = currentValue.toString().removeCharacters("\"\'");
		//refFileName = fileName;
		refFileName = fileNameInScript;

		String fileContent = dynamic_cast<Processor*>(hiseSpecialData->processor)->getMainController()->getExternalScriptFromCollection(fileNameInScript);
#endif

		return fileContent;
	};

	Statement* parseExternalFile()
	{
		match(TokenTypes::openParen);
		

		String refFileName;
		String fileContent = getFileContent(currentValue.toString(), refFileName);
        
		if (fileContent.isEmpty())
		{
			match(TokenTypes::literal);
			match(TokenTypes::closeParen);
			match(TokenTypes::semicolon);

			return new Statement(location);
		}
		else
		{
#if USE_BACKEND
            
			hiseSpecialData->includedFiles.add(new ExternalFileData(File(refFileName)));
#endif

			try
			{
				ExpressionTreeBuilder ftb(fileContent, refFileName);

				ftb.hiseSpecialData = hiseSpecialData;
				ftb.currentNamespace = hiseSpecialData;

				//ftb.setupApiData(*hiseSpecialData, fileContent);

				ScopedPointer<BlockStatement> s = ftb.parseStatementList();

				match(TokenTypes::literal);
				match(TokenTypes::closeParen);
				match(TokenTypes::semicolon);

				return s.release();
			}
			catch (String &errorMessage)
			{
				hiseSpecialData->includedFiles.getLast()->setErrorMessage(errorMessage);

				throw errorMessage;
			}
		}
	}

	Expression* matchEndOfStatement(Expression* ex)  { ExpPtr e(ex); if (currentType != TokenTypes::eof) match(TokenTypes::semicolon); return e.release(); }
	Expression* matchCloseParen(Expression* ex)      { ExpPtr e(ex); match(TokenTypes::closeParen); return e.release(); }

	Statement* parseIf()
	{
		ScopedPointer<IfStatement> s(new IfStatement(location));
		match(TokenTypes::openParen);
		s->condition = parseExpression();
		match(TokenTypes::closeParen);
		s->trueBranch = parseStatement();
		s->falseBranch = matchIf(TokenTypes::else_) ? parseStatement() : new Statement(location);
		return s.release();
	}

	Statement *parseRegisterAssignment(const Identifier &id)
	{
		match(TokenTypes::identifier);
		match(TokenTypes::assign);

		//const int index = registerIdentifiers.indexOf(id);

		const int index = hiseSpecialData->varRegister.getRegisterIndex(id);

		RegisterAssignment *r = new RegisterAssignment(location, index, parseExpression());

		match(TokenTypes::semicolon);
		return r;
	}

	Statement* parseReturn()
	{
		if (matchIf(TokenTypes::semicolon))
			return new ReturnStatement(location, new Expression(location));

		ReturnStatement* r = new ReturnStatement(location, parseExpression());
		matchIf(TokenTypes::semicolon);
		return r;
	}

	Statement* parseVar()
	{
		ScopedPointer<VarStatement> s(new VarStatement(location));
		s->name = parseIdentifier();

		hiseSpecialData->checkIfExistsInOtherStorage(HiseSpecialData::VariableStorageType::RootScope, s->name, location);

		s->initialiser = matchIf(TokenTypes::assign) ? parseExpression() : new Expression(location);

		if (matchIf(TokenTypes::comma))
		{
			ScopedPointer<BlockStatement> block(new BlockStatement(location));
			block->statements.add(s.release());
			block->statements.add(parseVar());
			return block.release();
		}

		match(TokenTypes::semicolon);
		return s.release();
	}

	Statement* parseConstVar(JavascriptNamespace* ns)
	{
		matchIf(TokenTypes::var);

		ScopedPointer<ConstVarStatement> s(new ConstVarStatement(location));

		s->name = parseIdentifier();

		hiseSpecialData->checkIfExistsInOtherStorage(HiseSpecialData::VariableStorageType::ConstVariables, s->name, location);

		s->initialiser = matchIf(TokenTypes::assign) ? parseExpression() : new Expression(location);

		if (matchIf(TokenTypes::comma))
		{
			ScopedPointer<BlockStatement> block(new BlockStatement(location));
			block->statements.add(s.release());
			block->statements.add(parseVar());
			return block.release();
		}

		jassert(ns->constObjects.contains(s->name));

		static const var uninitialised("uninitialised");
		ns->constObjects.set(s->name, uninitialised); // Will be initialied at runtime
		s->ns = ns;

		return s.release();
	}

	Statement *parseRegisterVar(JavascriptNamespace* ns, TokenIterator* preparser=nullptr)
	{
		if (preparser)
		{
			Identifier name = preparser->currentValue.toString();

			ns->varRegister.addRegister(name, var::undefined);

			const int index = ns->varRegister.getRegisterIndex(name);

			DebugableObject::Location loc;

			loc.fileName = preparser->location.externalFile;
			loc.charNumber = preparser->location.location - preparser->location.program.getCharPointer();

			ns->registerLocations.add(loc);

			jassert(ns->registerLocations.size() == ns->varRegister.getNumUsedRegisters());

			return nullptr;
		}
		else
		{
			ScopedPointer<RegisterVarStatement> s(new RegisterVarStatement(location));

			s->name = parseIdentifier();

			hiseSpecialData->checkIfExistsInOtherStorage(HiseSpecialData::VariableStorageType::Register, s->name, location);

			const int index = ns->varRegister.getRegisterIndex(s->name);
			s->varRegister = &ns->varRegister;

			s->initialiser = matchIf(TokenTypes::assign) ? parseExpression() : new Expression(location);

			if (matchIf(TokenTypes::comma))
			{
				ScopedPointer<BlockStatement> block(new BlockStatement(location));
				block->statements.add(s.release());
				block->statements.add(parseVar());
				return block.release();
			}

			match(TokenTypes::semicolon);
			return s.release();
		}
	}

	Statement* parseLockStatement(bool isReadLock)
	{
	
		ScopedPointer<LockStatement> ls = new LockStatement(location, isReadLock);

		match(TokenTypes::openParen);

		ls->lockedObj = parseFactor();

		match(TokenTypes::closeParen);
		match(TokenTypes::semicolon);

		return ls.release();
	}

	Statement* parseGlobalAssignment()
	{
		ScopedPointer<GlobalVarStatement> s(new GlobalVarStatement(location));
		s->name = parseIdentifier();
		
		if (!hiseSpecialData->globals->hasProperty(s->name))
		{
			hiseSpecialData->globals->setProperty(s->name, var::undefined());
		}
		
		s->initialiser = matchIf(TokenTypes::assign) ? parseExpression() : new Expression(location);
		
		if (matchIf(TokenTypes::comma))
		{
			ScopedPointer<BlockStatement> block(new BlockStatement(location));
			block->statements.add(s.release());
			block->statements.add(parseVar());
			return block.release();
		}

		match(TokenTypes::semicolon);
		return s.release();
	}

	Statement* parseLocalAssignment()
	{
		if (currentlyParsingInlineFunction)
		{
			InlineFunction::Object::Ptr ifo = dynamic_cast<InlineFunction::Object*>(getCurrentInlineFunction());

			ScopedPointer<LocalVarStatement> s(new LocalVarStatement(location, ifo));
			s->name = parseIdentifier();
			
			hiseSpecialData->checkIfExistsInOtherStorage(HiseSpecialData::VariableStorageType::LocalScope, s->name, location);

			ifo->localProperties.set(s->name, var::undefined());

			s->initialiser = matchIf(TokenTypes::assign) ? parseExpression() : new Expression(location);

			if (matchIf(TokenTypes::comma))
			{
				ScopedPointer<BlockStatement> block(new BlockStatement(location));
				block->statements.add(s.release());
				block->statements.add(parseVar());
				return block.release();
			}

			match(TokenTypes::semicolon);
			return s.release();
		}
		else if (!currentlyParsedCallback.isNull())
		{
			Callback* callback = hiseSpecialData->getCallback(currentlyParsedCallback);

			ScopedPointer<CallbackLocalStatement> s(new CallbackLocalStatement(location, callback));
			s->name = parseIdentifier();
			
			hiseSpecialData->checkIfExistsInOtherStorage(HiseSpecialData::VariableStorageType::LocalScope, s->name, location);

			callback->localProperties.set(s->name, var());

			s->initialiser = matchIf(TokenTypes::assign) ? parseExpression() : new Expression(location);

			if (matchIf(TokenTypes::comma))
			{
				ScopedPointer<BlockStatement> block(new BlockStatement(location));
				block->statements.add(s.release());
				block->statements.add(parseVar());
				return block.release();
			}

			match(TokenTypes::semicolon);
			return s.release();
		}

		throwError("Cannot define local variables outside of inline functions or callbacks.");

		return nullptr;
	}

	Statement* parseCallback()
	{
		Identifier name = parseIdentifier();

		Callback *c = hiseSpecialData->getCallback(name);

		jassert(c != nullptr);

		match(TokenTypes::openParen);

		for (int i = 0; i < c->getNumArgs(); i++)
		{
			c->parameters[i] = parseIdentifier();
			c->parameterValues[i] = var::undefined();
			
			if (i != c->getNumArgs() - 1) match(TokenTypes::comma);
		}

		match(TokenTypes::closeParen);

		ScopedValueSetter<Identifier> cParser(currentlyParsedCallback, name, Identifier::null);

		ScopedPointer<BlockStatement> s = parseBlock();

		c->setStatements(s.release());

		return new Statement(location);
	}

	Statement* parseNamespace()
	{
		Identifier namespaceId = parseIdentifier();

		currentNamespace = hiseSpecialData->getNamespace(namespaceId);

		if (currentNamespace == nullptr)
		{
			location.throwError("Error at parsing namespace");
		}

		ScopedPointer<BlockStatement> block = parseBlock();
		
		currentNamespace = hiseSpecialData;

		return block.release();
	}

	Statement* parseFunction()
	{
		Identifier name;

		if (hiseSpecialData->getCallback(currentValue.toString()))
		{
			return parseCallback();
		}

		var fn = parseFunctionDefinition(name);

		

		if (name.isNull())
			throwError("Functions defined at statement-level must have a name");

		ExpPtr nm(new UnqualifiedName(location, name)), value(new LiteralValue(location, fn));
		return new Assignment(location, nm, value);
	}

	InlineFunction::Object *getInlineFunction(Identifier &id, JavascriptNamespace* ns=nullptr)
	{
		if (ns == nullptr)
		{
			for (int i = 0; i < hiseSpecialData->inlineFunctions.size(); i++)
			{
				DynamicObject *o = hiseSpecialData->inlineFunctions.getUnchecked(i);

				InlineFunction::Object *obj = dynamic_cast<InlineFunction::Object*>(o);

				jassert(obj != nullptr);

				if (obj->name == id) return obj;
			}
		}
		else
		{
			for (int i = 0; i < ns->inlineFunctions.size(); i++)
			{
				DynamicObject *o = ns->inlineFunctions.getUnchecked(i);

				InlineFunction::Object *obj = dynamic_cast<InlineFunction::Object*>(o);

				jassert(obj != nullptr);

				if (obj->name == id) return obj;
			}
		}

		return nullptr;
	}

	int getRegisterIndex(const Identifier& id, JavascriptNamespace* ns = nullptr)
	{
		if (ns == nullptr)
		{
			return hiseSpecialData->varRegister.getRegisterIndex(id);
		}
		else
		{
			return ns->varRegister.getRegisterIndex(id);
		}
	}

	var* getRegisterData(int index, JavascriptNamespace* ns = nullptr)
	{
		if (ns == nullptr)
		{
			return hiseSpecialData->varRegister.getVarPointer(index);
		}
		else
		{
			return ns->varRegister.getVarPointer(index);
		}
	}

	int getConstIndex(const Identifier& id, JavascriptNamespace* ns = nullptr)
	{
		if (ns == nullptr)
		{
			return hiseSpecialData->constObjects.indexOf(id);
		}
		else
		{
			return ns->constObjects.indexOf(id);
		}
	}
	
	var* getConstData(int index, JavascriptNamespace* ns = nullptr)
	{
		if (ns == nullptr)
		{
			return hiseSpecialData->constObjects.getVarPointerAt(index);
		}
		else
		{
			return ns->constObjects.getVarPointerAt(index);
		}
	}

	DynamicObject* getCurrentInlineFunction()
	{
		return getCurrentNamespace()->inlineFunctions.getLast();
	}

	Expression* parseInlineFunctionCall(InlineFunction::Object *obj)
	{
		ScopedPointer<InlineFunction::FunctionCall> f = new InlineFunction::FunctionCall(location, obj);

		parseIdentifier();

		match(TokenTypes::openParen);

		while (currentType != TokenTypes::closeParen)
		{
			f->addParameter(parseExpression());
			if (currentType != TokenTypes::closeParen)
				match(TokenTypes::comma);
		}

		if (f->numArgs != f->parameterExpressions.size())
		{

			throwError("Inline function call " + obj->name + ": parameter amount mismatch: " + String(f->parameterExpressions.size()) + " (Expected: " + String(f->numArgs) + ")");
		}

		return matchCloseParen(f.release());
		
	}

	Statement *parseInlineFunction(JavascriptNamespace* ns, TokenIterator *preparser=nullptr)
	{
		if (preparser != nullptr)
		{
			int charNumber = preparser->location.location - preparser->location.program.getCharPointer();
			String fileName = preparser->location.externalFile;

			preparser->match(TokenTypes::function);
			Identifier name = preparser->currentValue.toString();
			preparser->match(TokenTypes::identifier);
			preparser->match(TokenTypes::openParen);

			Array<Identifier> inlineArguments;

			while (preparser->currentType != TokenTypes::closeParen)
			{
				inlineArguments.add(preparser->currentValue.toString());
				preparser->match(TokenTypes::identifier);
				if (preparser->currentType != TokenTypes::closeParen)
					preparser->match(TokenTypes::comma);
			}

			preparser->match(TokenTypes::closeParen);

			ScopedPointer<InlineFunction::Object> o = new InlineFunction::Object(name, inlineArguments);

			o->location.charNumber = charNumber;
			o->location.fileName = fileName;

			ns->inlineFunctions.add(o.release());
			preparser->matchIf(TokenTypes::semicolon);

			return nullptr;
		}
		else
		{
			if (currentlyParsingInlineFunction) throwError("No nested inline functions allowed.");

			match(TokenTypes::function);

			Identifier name = parseIdentifier();

			match(TokenTypes::openParen);

			while (currentType != TokenTypes::closeParen) skip();

			match(TokenTypes::closeParen);

			currentlyParsingInlineFunction = true;

			InlineFunction::Object::Ptr o = nullptr;

			for (int i = 0; i < ns->inlineFunctions.size(); i++)
			{
				if (o = dynamic_cast<InlineFunction::Object*>(ns->inlineFunctions[i].get()))
				{
					if (o->name == name)
					{
						break;
					}
				}
			}

			if (o != nullptr)
			{
				o->commentDoc = lastComment;
				clearLastComment();

				ScopedPointer<BlockStatement> body = parseBlock();

				o->body = body.release();

				currentlyParsingInlineFunction = false;

				matchIf(TokenTypes::semicolon);

				return new Statement(location);
			}
			else
			{
				currentlyParsingInlineFunction = false;

				location.throwError("Error at inline function parsing");
			}
		}
	}

	Statement* parseExternalCFunction()
	{
		match(TokenTypes::literal);
		match(TokenTypes::openBrace);

		

		String::CharPointerType start = location.location;

		static const Identifier void_("void");
		const bool hasReturnType = parseIdentifier() != void_;
		const Identifier name = parseIdentifier();

		const String comment = lastComment;

		match(TokenTypes::openParen);

		Array<Identifier> arguments;

		while (currentType != TokenTypes::closeParen)
		{
			match(TokenTypes::var);

			arguments.add(currentValue.toString());
			match(TokenTypes::identifier);

			if (currentType != TokenTypes::closeParen)
				match(TokenTypes::comma);
		}

		match(TokenTypes::closeParen);

		int braceLevel = 0;

		while (braceLevel >= 0 && currentType != TokenTypes::eof)
		{
			if (currentType == TokenTypes::openBrace)	braceLevel++;
			if (currentType == TokenTypes::closeBrace)	braceLevel--;

			skip();
		}

		String::CharPointerType end = location.location;
		String cCode = String(start, end - 1); 
		

		ScopedPointer<ExternalCFunction> functionObject = new ExternalCFunction(location, name, hasReturnType, arguments, comment, cCode);

		hiseSpecialData->externalCFunctions.add(functionObject.release());

		return new Statement(location);
	}

	Expression* parseExternalCFunctionCall()
	{
		Identifier name = parseIdentifier();

		int index = hiseSpecialData->getExternalCIndex(name);

		ExternalCFunction* cFunc = hiseSpecialData->externalCFunctions[index];

		ScopedPointer<ExternalCFunction::FunctionCall> fCall = new ExternalCFunction::FunctionCall(location, cFunc);

		match(TokenTypes::openParen);

		while (currentType != TokenTypes::closeParen)
		{
			fCall->parameterExpressions.add(parseExpression());
			if (currentType != TokenTypes::closeParen)
				match(TokenTypes::comma);
		}

		if (fCall->parameterExpressions.size() != cFunc->numArguments)
		{
			throwError("External C function call " + name.toString() + ": parameter amount mismatch: " + String(fCall->parameterExpressions.size()) + " (Expected: " + String(cFunc->numArguments) + ")");
		}

		return matchCloseParen(fCall.release());
	}

	Statement* parseCaseStatement()
	{
		const bool isNotDefaultCase = currentType == TokenTypes::case_;

		ScopedPointer<CaseStatement> s(new CaseStatement(location, isNotDefaultCase));

		skip();

		if (isNotDefaultCase) s->conditions.add(parseExpression());

		match(TokenTypes::colon);

		if (currentType == TokenTypes::openBrace)
		{
			s->body = parseBlock();
		}
		else if (currentType == TokenTypes::case_ || currentType == TokenTypes::default_ || currentType == TokenTypes::closeBrace)
		{
			// Empty statement (the condition will be added to the next case.
			s->body = nullptr;
		}
		else
		{
			s->body = new BlockStatement(location);

			while (currentType != TokenTypes::case_ && currentType != TokenTypes::closeBrace && currentType != TokenTypes::default_)
			{
				s->body->statements.add(parseStatement());
			}
		}

		return s.release();
	}

	Statement* parseSwitchBlock()
	{
		ScopedPointer<SwitchStatement> s(new SwitchStatement(location));

		match(TokenTypes::openParen);
		s->condition = parseExpression();
		match(TokenTypes::closeParen);
		match(TokenTypes::openBrace);

		Array<ExpPtr> emptyCaseConditions;

		while (currentType == TokenTypes::case_ || currentType == TokenTypes::default_)
		{
			ScopedPointer<CaseStatement> caseStatement = dynamic_cast<CaseStatement*>(parseCaseStatement());

			if (caseStatement != nullptr)
			{
				if (caseStatement->body == nullptr)
				{
					emptyCaseConditions.addArray(caseStatement->conditions);
					continue;
				}
				else
				{
					caseStatement->conditions.addArray(emptyCaseConditions);
					emptyCaseConditions.clear();
				}

				if (caseStatement->isNotDefault)
				{
					s->cases.add(caseStatement.release());
				}
				else
				{
					s->defaultCase = caseStatement.release();
				}
			}
		}

		match(TokenTypes::closeBrace);

		return s.release();
	}

	Statement* parseForLoop()
	{
		match(TokenTypes::openParen);

		const bool isVarInitialiser = matchIf(TokenTypes::var);
		
		Expression *iter = parseExpression();

		if (!isVarInitialiser && currentType == TokenTypes::closeParen)
		{
			ScopedPointer<LoopStatement> s(new LoopStatement(location, false, true));

			s->currentIterator = iter;

			s->iterator = nullptr;
			s->initialiser = nullptr;
			s->condition = new LiteralValue(location, true);

			match(TokenTypes::closeParen);

			s->body = parseStatement();
			return s.release();
		}
		else
		{
			ScopedPointer<LoopStatement> s(new LoopStatement(location, false));

			s->initialiser = matchEndOfStatement(iter);

			if (matchIf(TokenTypes::semicolon))
				s->condition = new LiteralValue(location, true);
			else
			{
				s->condition = parseExpression();
				match(TokenTypes::semicolon);
			}

			if (matchIf(TokenTypes::closeParen))
				s->iterator = new Statement(location);
			else
			{
				s->iterator = parseExpression();
				match(TokenTypes::closeParen);
			}

			s->body = parseStatement();
			return s.release();
		}



	}

	Statement* parseDoOrWhileLoop(bool isDoLoop)
	{
		ScopedPointer<LoopStatement> s(new LoopStatement(location, isDoLoop));
		s->initialiser = new Statement(location);
		s->iterator = new Statement(location);

		if (isDoLoop)
		{
			s->body = parseBlock();
			match(TokenTypes::while_);
		}

		match(TokenTypes::openParen);
		s->condition = parseExpression();
		match(TokenTypes::closeParen);

		if (!isDoLoop)
			s->body = parseStatement();

		return s.release();
	}

	Identifier parseIdentifier()
	{
		Identifier i;
		if (currentType == TokenTypes::identifier)
			i = currentValue.toString();

		match(TokenTypes::identifier);
		return i;
	}

	var parseFunctionDefinition(Identifier& functionName)
	{
		const String::CharPointerType functionStart(location.location);

		if (currentType == TokenTypes::identifier)
			functionName = parseIdentifier();

		ScopedPointer<FunctionObject> fo(new FunctionObject());

		
		parseFunctionParamsAndBody(*fo);
		fo->functionCode = String(functionStart, location.location);
        fo->createFunctionDefinition(functionName);
		fo->commentDoc = lastComment;
		clearLastComment();
		return var(fo.release());
	}

	Expression* parseFunctionCall(FunctionCall* call, ExpPtr& function)
	{
		ScopedPointer<FunctionCall> s(call);
		s->object = function;
		match(TokenTypes::openParen);

		while (currentType != TokenTypes::closeParen)
		{
			s->arguments.add(parseExpression());
			if (currentType != TokenTypes::closeParen)
				match(TokenTypes::comma);
		}

		return matchCloseParen(s.release());
	}

	Expression* parseApiExpression()
	{
		const Identifier apiId = parseIdentifier();
		const int apiIndex = hiseSpecialData->apiIds.indexOf(apiId);
		ApiClass *apiClass = hiseSpecialData->apiClasses.getUnchecked(apiIndex);

		match(TokenTypes::dot);

		const Identifier memberName = parseIdentifier();

		int constantIndex = apiClass->getConstantIndex(memberName);

		if (constantIndex != -1)
		{
			return parseApiConstant(apiClass, memberName);
		}
		else
		{
			return parseApiCall(apiClass, memberName);
		}
	}

	Expression* parseApiConstant(ApiClass *apiClass, const Identifier &constantName)
	{
		const int index = apiClass->getConstantIndex(constantName);

		const var value = apiClass->getConstantValue(index);

		ScopedPointer<ApiConstant> s = new ApiConstant(location);
		s->value = value;

		return s.release();
	}

	Expression* parseApiCall(ApiClass *apiClass, const Identifier &functionName)
	{
		int functionIndex, numArgs;
		apiClass->getIndexAndNumArgsForFunction(functionName, functionIndex, numArgs);

		const String prettyName = apiClass->getName() + "." + functionName.toString();

		if (functionIndex < 0) throwError("Function / constant not found: " + prettyName); // Handle also missing constants here
		ScopedPointer<ApiCall> s = new ApiCall(location, apiClass, numArgs, functionIndex);

		match(TokenTypes::openParen);

		int numActualArguments = 0;

		while (currentType != TokenTypes::closeParen)
		{
			if (numActualArguments < numArgs)
			{
				s->argumentList[numActualArguments++] = parseExpression();

				if (currentType != TokenTypes::closeParen)
					match(TokenTypes::comma);
			}
			else throwError("Too many arguments in API call " + prettyName + "(). Expected: " + String(numArgs));
		}

		if (numArgs != numActualArguments) throwError("Call to " + prettyName + "(): argument number mismatch : " + String(numActualArguments) + " (Expected : " + String(numArgs) + ")");

		return matchCloseParen(s.release());
	}

	Expression* parseConstExpression(JavascriptNamespace* ns=nullptr)
	{
		const Identifier constId = parseIdentifier();
		const int index = getConstIndex(constId, ns);

#if 0
		if (currentType == TokenTypes::dot)
		{
			match(TokenTypes::dot);
			const Identifier memberName = parseIdentifier();

			return parseConstObjectApiCall(constId, memberName, ns);
		}
#endif

		ns = (ns != nullptr) ? ns : hiseSpecialData;

		return new ConstReference(location, ns, index);
	}

	Expression* parseConstObjectApiCall(const Identifier& objectName, const Identifier& functionName, JavascriptNamespace* ns=nullptr)
	{
		const String prettyName = objectName.toString() + "." + functionName.toString();

		const int index = getConstIndex(objectName, ns);
		var *v = getConstData(index, ns);

		ScopedPointer<ConstObjectApiCall> s = new ConstObjectApiCall(location, v, functionName);

		match(TokenTypes::openParen);

		int numActualArguments = 0;

		while (currentType != TokenTypes::closeParen)
		{
			s->argumentList[numActualArguments++] = parseExpression();

			if (currentType != TokenTypes::closeParen)
				match(TokenTypes::comma);
		}

		return matchCloseParen(s.release());
	}

	Expression* parseSuffixes(Expression* e)
	{
		ExpPtr input(e);

		if (matchIf(TokenTypes::dot))
			return parseSuffixes(new DotOperator(location, input, parseIdentifier()));

		if (currentType == TokenTypes::openParen)
			return parseSuffixes(parseFunctionCall(new FunctionCall(location), input));

		if (matchIf(TokenTypes::openBracket))
		{
			ScopedPointer<ArraySubscript> s(new ArraySubscript(location));
			s->object = input;
			s->index = parseExpression();
			match(TokenTypes::closeBracket);
			return parseSuffixes(s.release());
		}

		if (matchIf(TokenTypes::plusplus))   return parsePostIncDec<AdditionOp>(input);
		if (matchIf(TokenTypes::minusminus)) return parsePostIncDec<SubtractionOp>(input);

		return input.release();
	}

	Expression* parseFactor(JavascriptNamespace* ns=nullptr)
	{
		if (currentType == TokenTypes::identifier)
		{
			Identifier id = Identifier(currentValue.toString());

			// Allow direct referencing of namespaced variables within the namespace
			if (getCurrentNamespace() != hiseSpecialData)
			{
				ns = getCurrentNamespace();
			}

			if (id == currentIterator)
			{
				return parseSuffixes(new LoopStatement::IteratorName(location, parseIdentifier()));
			}
			else if (currentlyParsingInlineFunction)
			{
				DynamicObject *o = nullptr;

				if (ns != nullptr)
				{
					o = ns->inlineFunctions.getLast();
				}
				else
				{
					o = hiseSpecialData->inlineFunctions.getLast();
				}

				jassert(o != nullptr);

				InlineFunction::Object* ob = dynamic_cast<InlineFunction::Object*>(o);

				const int inlineParameterIndex = ob->parameterNames.indexOf(id);
				const int localParameterIndex = ob->localProperties.indexOf(id);

				if (inlineParameterIndex >= 0)
				{
					parseIdentifier();
					return parseSuffixes(new InlineFunction::ParameterReference(location, ob, inlineParameterIndex));
				}
				if (localParameterIndex >= 0)
				{
					parseIdentifier();
					return parseSuffixes(new LocalReference(location, ob, id));
				}
			}

			// Only resolve one level of namespaces
			JavascriptNamespace* namespaceForId = (ns == nullptr) ? hiseSpecialData->getNamespace(id) : nullptr;

			if (namespaceForId != nullptr)
			{
				match(TokenTypes::identifier);
				match(TokenTypes::dot);
				
				return parseFactor(namespaceForId);
			}
			else
			{
				InlineFunction::Object *obj = getInlineFunction(id, ns);
				const int registerIndex = getRegisterIndex(id, ns);
				const int apiClassIndex = hiseSpecialData->apiIds.indexOf(id);
				const int constIndex = getConstIndex(id, ns);
				const int globalIndex = hiseSpecialData->globals->getProperties().indexOf(id);
				const int externalCIndex = hiseSpecialData->getExternalCIndex(id);

				if (obj != nullptr)
				{
					return parseSuffixes(parseInlineFunctionCall(obj));
				}
				else if (apiClassIndex != -1)
				{
					return parseSuffixes(parseApiExpression());
				}
				else if (constIndex != -1)
				{
					return parseSuffixes(parseConstExpression(ns));
				}
				else if (externalCIndex != -1)
				{
					return parseSuffixes(parseExternalCFunctionCall());
				}
				else if (registerIndex != -1)
				{
					VarRegister* rootRegister = (ns != nullptr) ? &ns->varRegister : &hiseSpecialData->varRegister;

					return parseSuffixes(new RegisterName(location, parseIdentifier(), rootRegister, registerIndex, getRegisterData(registerIndex, ns)));
				}
				else if (globalIndex != -1)
				{
					return parseSuffixes(new GlobalReference(location, hiseSpecialData->globals, parseIdentifier()));
				}
				else
				{
					if (!currentlyParsedCallback.isNull())
					{
						Callback *c = hiseSpecialData->getCallback(currentlyParsedCallback);

						if (c != nullptr)
						{
							var* callbackParameter = c->getVarPointer(id);

							if (callbackParameter != nullptr)
							{
								parseIdentifier();
								return parseSuffixes(new CallbackParameterReference(location, callbackParameter));
							}

							var* localParameter = c->localProperties.getVarPointer(id);

							if (localParameter != nullptr)
							{
								parseIdentifier();
								return parseSuffixes(new CallbackLocalReference(location, localParameter));
							}
						}
						else
						{
							jassertfalse;
						}
					}

					return parseSuffixes(new UnqualifiedName(location, parseIdentifier()));
				}
			}
		}
		if (matchIf(TokenTypes::openParen))        return parseSuffixes(matchCloseParen(parseExpression()));
		if (matchIf(TokenTypes::true_))            return parseSuffixes(new LiteralValue(location, (int)1));
		if (matchIf(TokenTypes::false_))           return parseSuffixes(new LiteralValue(location, (int)0));
		if (matchIf(TokenTypes::null_))            return parseSuffixes(new LiteralValue(location, var()));
		if (matchIf(TokenTypes::undefined))        return parseSuffixes(new Expression(location));

		if (currentType == TokenTypes::literal)
		{
			var v(currentValue); skip();
			return parseSuffixes(new LiteralValue(location, v));
		}

		if (matchIf(TokenTypes::openBrace))
		{
			ScopedPointer<ObjectDeclaration> e(new ObjectDeclaration(location));

			while (currentType != TokenTypes::closeBrace)
			{
				e->names.add(currentValue.toString());
				match((currentType == TokenTypes::literal && currentValue.isString())
					? TokenTypes::literal : TokenTypes::identifier);
				match(TokenTypes::colon);
				e->initialisers.add(parseExpression());

				if (currentType != TokenTypes::closeBrace)
					match(TokenTypes::comma);
			}

			match(TokenTypes::closeBrace);
			return parseSuffixes(e.release());
		}

		if (matchIf(TokenTypes::openBracket))
		{
			ScopedPointer<ArrayDeclaration> e(new ArrayDeclaration(location));

			while (currentType != TokenTypes::closeBracket)
			{
				e->values.add(parseExpression());

				if (currentType != TokenTypes::closeBracket)
					match(TokenTypes::comma);
			}

			match(TokenTypes::closeBracket);
			return parseSuffixes(e.release());
		}

		if (matchIf(TokenTypes::function))
		{
			Identifier name;
			var fn = parseFunctionDefinition(name);

			if (name.isValid())
				throwError("Inline functions definitions cannot have a name");

			return new LiteralValue(location, fn);
		}

		if (matchIf(TokenTypes::new_))
		{
			ExpPtr name(new UnqualifiedName(location, parseIdentifier()));

			while (matchIf(TokenTypes::dot))
				name = new DotOperator(location, name, parseIdentifier());

			return parseFunctionCall(new NewOperator(location), name);
		}

		throwError("Found " + getTokenName(currentType) + " when expecting an expression");
		return nullptr;
	}

	template <typename OpType>
	Expression* parsePreIncDec()
	{
		Expression* e = parseFactor(); // careful - bare pointer is deliberately alised
		ExpPtr lhs(e), one(new LiteralValue(location, (int)1));
		return new SelfAssignment(location, e, new OpType(location, lhs, one));
	}

	template <typename OpType>
	Expression* parsePostIncDec(ExpPtr& lhs)
	{
		Expression* e = lhs.release(); // careful - bare pointer is deliberately alised
		ExpPtr lhs2(e), one(new LiteralValue(location, (int)1));
		return new PostAssignment(location, e, new OpType(location, lhs2, one));
	}

	Expression* parseTypeof()
	{
		ScopedPointer<FunctionCall> f(new FunctionCall(location));
		f->object = new UnqualifiedName(location, "typeof");
		f->arguments.add(parseUnary());
		return f.release();
	}

	Expression* parseUnary()
	{
		if (matchIf(TokenTypes::minus))       { ExpPtr a(new LiteralValue(location, (int)0)), b(parseUnary()); return new SubtractionOp(location, a, b); }
		if (matchIf(TokenTypes::logicalNot))  { ExpPtr a(new LiteralValue(location, (int)0)), b(parseUnary()); return new EqualsOp(location, a, b); }
		if (matchIf(TokenTypes::plusplus))    return parsePreIncDec<AdditionOp>();
		if (matchIf(TokenTypes::minusminus))  return parsePreIncDec<SubtractionOp>();
		if (matchIf(TokenTypes::typeof_))     return parseTypeof();

		return parseFactor();
	}

	Expression* parseMultiplyDivide()
	{
		ExpPtr a(parseUnary());

		for (;;)
		{
			if (matchIf(TokenTypes::times))        { ExpPtr b(parseUnary()); a = new MultiplyOp(location, a, b); }
			else if (matchIf(TokenTypes::divide))  { ExpPtr b(parseUnary()); a = new DivideOp(location, a, b); }
			else if (matchIf(TokenTypes::modulo))  { ExpPtr b(parseUnary()); a = new ModuloOp(location, a, b); }
			else break;
		}

		return a.release();
	}

	Expression* parseAdditionSubtraction()
	{
		ExpPtr a(parseMultiplyDivide());

		for (;;)
		{
			if (matchIf(TokenTypes::plus))            { ExpPtr b(parseMultiplyDivide()); a = new AdditionOp(location, a, b); }
			else if (matchIf(TokenTypes::minus))      { ExpPtr b(parseMultiplyDivide()); a = new SubtractionOp(location, a, b); }
			else break;
		}

		return a.release();
	}

	Expression* parseShiftOperator()
	{
		ExpPtr a(parseAdditionSubtraction());

		for (;;)
		{
			if (matchIf(TokenTypes::leftShift))                { ExpPtr b(parseExpression()); a = new LeftShiftOp(location, a, b); }
			else if (matchIf(TokenTypes::rightShift))          { ExpPtr b(parseExpression()); a = new RightShiftOp(location, a, b); }
			else if (matchIf(TokenTypes::rightShiftUnsigned))  { ExpPtr b(parseExpression()); a = new RightShiftUnsignedOp(location, a, b); }
			else break;
		}

		return a.release();
	}

	Expression* parseComparator()
	{
		ExpPtr a(parseShiftOperator());

		for (;;)
		{
			if (matchIf(TokenTypes::equals))                  { ExpPtr b(parseShiftOperator()); a = new EqualsOp(location, a, b); }
			else if (matchIf(TokenTypes::notEquals))          { ExpPtr b(parseShiftOperator()); a = new NotEqualsOp(location, a, b); }
			else if (matchIf(TokenTypes::typeEquals))         { ExpPtr b(parseShiftOperator()); a = new TypeEqualsOp(location, a, b); }
			else if (matchIf(TokenTypes::typeNotEquals))      { ExpPtr b(parseShiftOperator()); a = new TypeNotEqualsOp(location, a, b); }
			else if (matchIf(TokenTypes::lessThan))           { ExpPtr b(parseShiftOperator()); a = new LessThanOp(location, a, b); }
			else if (matchIf(TokenTypes::lessThanOrEqual))    { ExpPtr b(parseShiftOperator()); a = new LessThanOrEqualOp(location, a, b); }
			else if (matchIf(TokenTypes::greaterThan))        { ExpPtr b(parseShiftOperator()); a = new GreaterThanOp(location, a, b); }
			else if (matchIf(TokenTypes::greaterThanOrEqual)) { ExpPtr b(parseShiftOperator()); a = new GreaterThanOrEqualOp(location, a, b); }
			else break;
		}

		return a.release();
	}

	Expression* parseLogicOperator()
	{
		ExpPtr a(parseComparator());

		for (;;)
		{
			if (matchIf(TokenTypes::logicalAnd))       { ExpPtr b(parseComparator()); a = new LogicalAndOp(location, a, b); }
			else if (matchIf(TokenTypes::logicalOr))   { ExpPtr b(parseComparator()); a = new LogicalOrOp(location, a, b); }
			else if (matchIf(TokenTypes::bitwiseAnd))  { ExpPtr b(parseComparator()); a = new BitwiseAndOp(location, a, b); }
			else if (matchIf(TokenTypes::bitwiseOr))   { ExpPtr b(parseComparator()); a = new BitwiseOrOp(location, a, b); }
			else if (matchIf(TokenTypes::bitwiseXor))  { ExpPtr b(parseComparator()); a = new BitwiseXorOp(location, a, b); }
			else break;
		}

		return a.release();
	}

	Expression* parseTerneryOperator(ExpPtr& condition)
	{
		ScopedPointer<ConditionalOp> e(new ConditionalOp(location));
		e->condition = condition;
		e->trueBranch = parseExpression();
		match(TokenTypes::colon);
		e->falseBranch = parseExpression();
		return e.release();
	}

	


	Array<Identifier> registerIdentifiers;

	Identifier currentIterator;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExpressionTreeBuilder)
};


void HiseJavascriptEngine::RootObject::ExpressionTreeBuilder::preprocessCode(const String& codeToPreprocess, const String& externalFileName)
{
	if (codeToPreprocess.isEmpty()) return;

	static const var undeclared("undeclared");

	JavascriptNamespace* root = hiseSpecialData;
	JavascriptNamespace* cns = root;
	TokenIterator it(codeToPreprocess, externalFileName);

	Array<Identifier> ids;

	int braceLevel = 0;

	while (it.currentType != TokenTypes::eof)
	{
		if (it.currentType == TokenTypes::namespace_)
		{
			if (cns != root)
			{
				it.location.throwError("Nesting of namespaces is not allowed");
			}

			for (int i = 0; i < ids.size(); i++)
				cns->constObjects.set(ids[i], undeclared);
			
			ids.clear();
			it.match(TokenTypes::namespace_);
			Identifier namespaceId = Identifier(it.currentValue);

			if (hiseSpecialData->getNamespace(namespaceId) == nullptr)
			{
				ScopedPointer<JavascriptNamespace> newNamespace = new JavascriptNamespace(namespaceId);
				cns = newNamespace;
				hiseSpecialData->namespaces.add(newNamespace.release());
				continue;
			}
			else
			{
				it.location.throwError("Duplicate namespace " + namespaceId.toString());
			}
		}

		// Skip extern "C" functions
		if (it.currentType == TokenTypes::extern_)
		{
			while (!(it.currentType == TokenTypes::closeBrace && braceLevel == 1) && 
				   !(it.currentType == TokenTypes::eof))
			{
				if (it.currentType == TokenTypes::openBrace) braceLevel++;
				else if (it.currentType == TokenTypes::closeBrace) braceLevel--;

				it.skip();
			}
		}

		// Search in included files
		if (it.currentType == TokenTypes::include_)
		{
			it.match(TokenTypes::include_);
			it.match(TokenTypes::openParen);
			String fileName = it.currentValue.toString();
			String externalCode = getFileContent(it.currentValue.toString(), fileName);
			
			preprocessCode(externalCode, fileName);

			continue;
		}

		// Handle the brace level
		if (it.matchIf(TokenTypes::openBrace))
		{
			braceLevel++;
			continue;
		}
		else if (it.matchIf(TokenTypes::closeBrace))
		{
			braceLevel--;
			if (braceLevel == 0 && (root != cns))
			{
				for (int i = 0; i < ids.size(); i++)
					cns->constObjects.set(ids[i], undeclared);
				
				ids.clear();
				cns = root;
			}
			
			continue;
		}

		if (it.matchIf(TokenTypes::inline_))
		{	
			parseInlineFunction(cns, &it);
			continue;
		}

		if (it.matchIf(TokenTypes::register_var))
		{
			parseRegisterVar(cns, &it);
			continue;
		}

		// Handle the keyword
		if (it.currentType == TokenTypes::const_)
		{
			it.match(TokenTypes::const_);
			it.matchIf(TokenTypes::var);

			const Identifier newId(it.currentValue);

			if ((root == cns) && braceLevel != 0) it.location.throwError("const var declaration must be on global level");
			if (newId.isNull())					  it.location.throwError("Expected identifier for const var declaration");
			if (ids.contains(newId))			  it.location.throwError("Duplicate const var declaration.");

			ids.add(newId);

			DebugableObject::Location loc;

			loc.fileName = it.location.externalFile;
			loc.charNumber = it.location.location - it.location.program.getCharPointer();

			cns->constLocations.add(loc);

			continue;
		}
		else
		{
			it.skip();
		}
	}

	if (root != cns)
	{
		it.location.throwError("Parsing error (open namespace)");
	}

	for (int i = 0; i < ids.size(); i++)
	{
		cns->constObjects.set(ids[i], undeclared);
	}

	jassert(cns->constObjects.size() == cns->constLocations.size());
}


var HiseJavascriptEngine::RootObject::evaluate(const String& code)
{
	ExpressionTreeBuilder tb(code, String());

	tb.setupApiData(hiseSpecialData, code);

	return ExpPtr(tb.parseExpression())->getResult(Scope(nullptr, this, this));
}

void HiseJavascriptEngine::RootObject::execute(const String& code, bool allowConstDeclarations)
{
	ExpressionTreeBuilder tb(code, String());

	tb.setupApiData(hiseSpecialData, allowConstDeclarations ? code : String());

	ScopedPointer<BlockStatement>(tb.parseStatementList())->perform(Scope(nullptr, this, this), nullptr);
}

HiseJavascriptEngine::RootObject::FunctionObject::FunctionObject(const FunctionObject& other) : DynamicObject(), functionCode(other.functionCode)
{
	ExpressionTreeBuilder tb(functionCode, String());

	tb.parseFunctionParamsAndBody(*this);
}