#include "compiler.h"
#include "parser.h"

//#define pop(X) ASSERT(X.size()>0); pop_sub(X);

void dump(vector<Expr*>& v) {
	for (int i=0; i<v.size(); i++) {
		v[i]->dump_top();
	}
	dbprintf("\n");
}
void pop_operator_call( vector<SrcOp>& operators,vector<Expr*>& operands) {
	//takes the topmost operator from the operator stack
	//creates an expression node calling it, consumes operands,
	//places result on operand stack
	
	auto op=pop(operators);
	auto * p=new ExprOp(op.op,op.pos);
	if (operands.size()>=2 && (arity(op.op)==2)){
		auto arg1=pop(operands);
		p->lhs=pop(operands);
		p->rhs=arg1;
	} else if (operands.size()>=1 && arity(op.op)==1){
		p->rhs=pop(operands);
		//		p->argls.push_back(pop(operands));
	} else{
		//						printf("\noperands:");dump(operands);
		//						printf("operators");dump(operators);
		error(0,"\nerror: %s arity %d, %lu operands given\n",str(op.op),arity(op.op),operands.size());
	}
	p->pos=p->lhs?p->lhs->pos:p->rhs->pos;
	operands.push_back((Expr*)p);
}
//   void fn(x:(int,int),y:(int,int))
void flush_op_stack(ExprBlock* block, vector<SrcOp>& ops,vector<Expr*>& vals) {
	while (ops.size()>0) pop_operator_call(ops,vals);
	while (vals.size()) {
		block->argls.push_back(pop(vals));
	}
}

ExprBlock* parse_block(TokenStream&src,int close,int delim, Expr* op);

/// parse_expr - parse a single expression
/// TODO - refactor 'parse_block', this is backwards!
Expr* parse_expr(TokenStream&src) {
	return parse_block(src,0,0,nullptr);
}

void another_operand_so_maybe_flush(bool& was_operand, ExprBlock* node,
									vector<SrcOp>& operators,
									vector<Expr*>& operands
									
									){
	if (was_operand==true) {
		//error(node,"warning undeliminated expression parsing anyway");
		flush_op_stack(node,operators,operands);// keep going
	}
	was_operand=true;
}
LLVMType Node::get_type_llvm() const
{
	if (!this) return LLVMType{VOID,0};
	if (!this->m_type) return LLVMType{VOID,0};
	auto tn=this->m_type->name;
	if (tn==VOID) return LLVMType{VOID,0};
	if (!this->m_type->sub) return LLVMType{tn,0};
	if (tn==PTR || tn==DEREF ||tn==ADDR ) return LLVMType{this->m_type->sub->name,true};
	// todo structs, etc - llvm DOES know about these.
	return LLVMType{0,0};
}

ExprMatch* parse_match(TokenStream& src){
	auto m=new ExprMatch(); m->pos=src.pos;
	m->expr=parse_expr(src);
	src.expect(OPEN_BRACE);
	MatchArm** pp=&m->arms;
	while (src.peek_tok()!=CLOSE_BRACE){
		auto a=new MatchArm();
		a->pos=src.pos;
		*pp=a;
		pp=&a->next;
		a->pattern=parse_pattern(src,FAT_ARROW,0);
		a->body=parse_expr(src);
		auto tok=src.expect(COMMA,CLOSE_BRACE);
		if (tok==CLOSE_BRACE)
			break;
	}
	return	m;
}

// todo: we're not sure we need a whole other parser
// couldn't expressions,types,patterns all use the same gramar & parser,
// & just interpret the nodes differently?
Pattern* parse_pattern(TokenStream& src,int close,int close2=0){
	Pattern* p=new Pattern(); p->pos=src.pos;auto first=p;p->name=0;
	while (auto t=src.eat_tok()){
		if (t==close || t==close2) break;
		if (t==OPEN_PAREN){
			if (!p->name){
				// tuple..
				p->name=TUPLE;
			}
			p->sub=parse_pattern(src,CLOSE_PAREN,0);
		}
		// todo - range ".."
		// todo - slice patterns
		else if (t==LET_ASSIGN || t==ASSIGN_COLON || t== ASSIGN ||t==PATTERN_BIND){ // todo its @ in scala,rust
			auto np=new Pattern;
			np->name=PATTERN_BIND;
			np->sub = p;
			first=np; // assert only once
		}
		else if (t==COMMA){
			p=p->next=new Pattern();
		} else if (t==OR && close!=CLOSE_PAREN){ // todo - bit more elaborate. should be ANY(....)  distinct from TUPLE(...)
			p=p->next=new Pattern();
		} else{
			p->name=t;
		}
	}
	return first;
}

// default ++p  is {next(p); p}
// default p++ is {let r=p; next(p); r}
//
// for (x,y) in stuff {
// }
// copy how rust iteration works.
// for iter=begin(stuff); valid(iter); next(iter) { (x,y)=extract(iter);   }
//
// desugars same as c++ for (auto p:rhs)
// for (auto p=rhs.begin(); p!=rhs.end(); ++p)
// no; crap idea.
//
//
void parse_fn_args_ret(ExprFnDef* fndef,TokenStream& src,int close){
	Name tok;
	while ((tok=src.peek_tok())!=NONE) {
		if (tok==ELIPSIS){
			fndef->variadic=true; src.eat_tok(); src.expect(CLOSE_PAREN); break;
		}
		if (src.eat_if(close)){break;}
		auto arg=parse_arg(src,close);
		fndef->args.push_back(arg);
		src.eat_if(COMMA);
	}
	// TODO: multiple argument blocks for currying?.
	if (src.eat_if(ARROW) || src.eat_if(COLON)) {
		fndef->ret_type = parse_type(src, 0,fndef);
	}
}
void parse_fn_body(ExprFnDef* fndef, TokenStream& src){
	// read function arguments
	// implicit "progn" here..
	auto tok=src.peek_tok();
	if (src.eat_if(OPEN_BRACE)){
		fndef->body = parse_block(src, CLOSE_BRACE, SEMICOLON, nullptr);
	} else if (tok==SEMICOLON || tok==COMMA || tok==CLOSE_BRACE ){
		fndef->body=nullptr; // Its' just an extern prototype.
		fndef->c_linkage=true;
	} else{  // its' a single-expression functoin, eg lambda.
		fndef->body=parse_expr(src);
	}
}

ExprFnDef* parse_fn(TokenStream&src, ExprStructDef* owner) {
	auto *fndef=new ExprFnDef(src.pos);
	// read function name or blank
	
	auto tok=src.eat_tok();
	if (tok!=OPEN_PAREN) {
		ASSERT(is_ident(tok));
		fndef->name=tok;
		if (src.eat_if(OPEN_BRACKET)) {
			parse_typeparams(src,fndef->typeparams);
		}
		tok=src.expect(OPEN_PAREN);
	} else {
		char tmp[512]; sprintf(tmp,"anon_fn_%d",src.pos.line);
		fndef->name=getStringIndex(tmp);
	}
	// todo: generalize: any named parameter might be 'this'.
	//.. but such functions will be outside the struct.
	if (owner){
		fndef->args.push_back(new ArgDef(src.pos,THIS,new Type(PTR,owner)));
	}
	parse_fn_args_ret(fndef,src,CLOSE_PAREN);
	parse_fn_body(fndef,src);
	return fndef;
}
ExprFnDef* parse_closure(TokenStream&src) {// eg |x|x*2
	// we read | to get here
	auto *fndef=new ExprFnDef(src.pos);
	// read function name or blank
	
	char tmp[512]; sprintf(tmp,"closure_%d",src.pos.line);
	fndef->name=getStringIndex(tmp);
	fndef->m_closure=true;
	
	parse_fn_args_ret(fndef,src,OR);
	parse_fn_body(fndef,src);
	return fndef;
}


ExprBlock* parse_block(TokenStream& src,int close,int delim, Expr* op) {
	// shunting yard expression parser+dispatch to other contexts
	ExprBlock *node=new ExprBlock(src.pos); node->call_expr=op;
	if (!g_pRoot) g_pRoot=node;
	verify(node->type());
	vector<SrcOp> operators;
	vector<Expr*> operands;
	bool	was_operand=false;
	int wrong_delim=delim==SEMICOLON?COMMA:SEMICOLON;
	int wrong_close=close==CLOSE_PAREN?CLOSE_BRACE:CLOSE_PAREN;
	node->bracket_type=(close==CLOSE_BRACKET)?OPEN_BRACKET:close==CLOSE_PAREN?OPEN_PAREN:close==CLOSE_BRACE?OPEN_BRACE:0;
	while (true) {
		if (src.peek_tok()==0) break;
		if (src.peek_tok()==IN) break;
		// parsing a single expression TODO split this into 'parse expr()', 'parse_compound'
		if (close || delim) { // compound expression mode.
			if (src.eat_if(close))
				break;
			if (src.eat_if(wrong_close)) {
				error(0,"unexpected %s, expected %s",getString(close),getString(wrong_close));
				error_end(0);
			}
		} else { // single expression mode - we dont consume delimiter.
			auto peek=src.peek_tok();
			if (peek==CLOSE_BRACKET || peek==CLOSE_BRACE || peek==COMMA || peek==SEMICOLON)
				break;
		}
		
		if (src.is_next_literal()) {
			auto ln=parse_literal(src);
			operands.push_back(ln);
			was_operand=true;
			continue;
		}
		else if (src.eat_if(STRUCT)){
			another_operand_so_maybe_flush(was_operand,node,operators,operands);
			operands.push_back(parse_struct(src));
		}
		else if (src.eat_if(ENUM)){
			another_operand_so_maybe_flush(was_operand,node,operators,operands);
			operands.push_back(parse_enum(src));
		}
		else if (src.eat_if(MATCH)){
			another_operand_so_maybe_flush(was_operand,node,operators,operands);
			operands.push_back(parse_match(src));
		}
		else if (src.eat_if(FN)) {
			another_operand_so_maybe_flush(was_operand,node,operators,operands);
			/// TODO local functions should be sugar for rolling a closure bound to a variable.
			auto local_fn=parse_fn(src,nullptr);
			operands.push_back(local_fn);
		}
		else if (src.eat_if(FOR)){
			another_operand_so_maybe_flush(was_operand,node,operators,operands);
			operands.push_back(parse_for(src));
		}
		else if (src.eat_if(IF)){
			another_operand_so_maybe_flush(was_operand,node,operators,operands);
			operands.push_back(parse_if(src));
		}
		else if (auto t=src.eat_if(BREAK,RETURN,CONTINUE)){
			another_operand_so_maybe_flush(was_operand,node,operators,operands);
			operands.push_back(parse_flow(src,t));
		}
		else if (src.eat_if(OPEN_PAREN)) {
			if (was_operand){
				operands.push_back(parse_block(src, CLOSE_PAREN,SEMICOLON, pop(operands)));
				// call result is operand
			}
			else {
				operands.push_back(parse_block(src,CLOSE_PAREN,COMMA,nullptr)); // just a subexpression
				was_operand=true;
			}
		} else if (src.eat_if(OPEN_BRACKET)){
			if (was_operand){
				operands.push_back(parse_block(src,CLOSE_BRACKET,COMMA,pop(operands)));
			} else {
				error(operands.back()?operands.back():node,"TODO: array initializer");
				operands.push_back(parse_block(src,CLOSE_PAREN,COMMA,nullptr)); // just a subexpression
				was_operand=true;
			}
		} else if (src.eat_if(OPEN_BRACE)){
			//			error(operands.back()?operands.back():node,"struct initializer");
			if (was_operand){// struct initializer
				operands.push_back(parse_block(src,CLOSE_BRACE,COMMA,pop(operands)));
			}
			else{//progn aka scope block with return value.
				auto sub=parse_block(src,CLOSE_BRACE,SEMICOLON,nullptr);
				operands.push_back(sub);
				if (sub->delimiter==COMMA)
					sub->create_anon_struct_initializer();
			}
		} else if (src.eat_if(delim)) {
			flush_op_stack(node,operators,operands);
			node->set_delim(delim);
			was_operand=false;
		}
		else if (src.eat_if(wrong_delim) && delim){ //allows ,,;,,;,,  TODO. more precise.
			node->set_delim(wrong_delim);
			flush_op_stack(node,operators,operands);// keep going
			was_operand=false;
		}
		else{
			auto tok=src.eat_tok();
			if (!was_operand && tok==OR){
				another_operand_so_maybe_flush(was_operand,node,operators,operands);
				operands.push_back(parse_closure(src));
			} else
				if (is_operator(tok)) {
					if (was_operand) tok=get_infix_operator(tok);
					else tok=get_prefix_operator(tok);
					
					
					verify_all();
					while (operators.size()>0) {
						int prev_precedence=precedence(operators.back().op);
						int prec=precedence(tok);
						if (prev_precedence>prec
							||(is_right_assoc(tok)&&prec==prev_precedence))
							break;
						pop_operator_call(operators,operands);
					}
					verify_all();
					if (tok==AS){
						Type *t=parse_type(src,0,nullptr);
						if (!was_operand) error(t,"as must follow operand");
						auto lhs=operands.back(); operands.pop_back();
						operands.push_back(new ExprOp(AS,src.pos,lhs,t));
						was_operand=true;
						t->set_type(t);
					}else
						if (tok==COLON){// special case: : invokes parsing type. TODO: we actually want to get rid of this? type could be read from other nodes, parsed same as rest?
							Type *t=parse_type(src,0,nullptr);
							auto lhs=operands.back();
							lhs->set_type(t);
							was_operand=true;
						} else if (tok==ASSIGN_COLON){ //x=:Type  ... creates a var of 'Type'.
							Type *t=parse_type(src,0,nullptr);
							operators.push_back(SrcOp{tok,src.pos});
							operands.push_back(t);
							was_operand=true;
						}
					
						else{
							operators.push_back(SrcOp{tok,src.pos});
							was_operand=false;
						}
				} else {
					another_operand_so_maybe_flush(was_operand,node,operators,operands);
					operands.push_back(new ExprIdent(tok,src.pos));
				}
		}
		//ASSERT(sub);
		//node->argls.push_back(sub);
	};
	if (operands.size()){
		// final expression is also returnvalue,
		flush_op_stack(node,operators,operands);
	} else if (node->is_compound_expression()){
		node->argls.push_back(new ExprLiteral(src.pos));
	}
	verify(node->get_type());
	node->verify();
	return node;
}

ExprLiteral* parse_literal(TokenStream& src) {
	ExprLiteral* ln=0;
	if (src.is_next_number()) {
		auto n=src.eat_number();
		if (n.denom==1) {ln=new ExprLiteral(src.pos,n.num);}
		else {ln=new ExprLiteral(src.pos, (float)n.num/(float)n.denom);}
	} else if (src.is_next_string()) {
		ln=new ExprLiteral(src.pos,src.eat_string());
	} else {
		error(0,"error parsing literal\n");
		error_end(0);
	}
	return ln;
}

void parse_ret_val(TokenStream& src, Node* owner, Type* fn_type){
	if (src.eat_if("->")){
		fn_type->push_back(parse_type(src,0,owner));// return value
	} else{
		fn_type->push_back(new Type(owner,VOID));// return value
	}
}
Type* parse_tuple(TokenStream& src, Node* owner)
{
	auto ret= new Type(TUPLE,src.pos);
	while (auto sub=parse_type(src, CLOSE_PAREN,owner)){
		ret->push_back(sub);
		src.eat_if(COMMA);
	}
	return ret;
}
Type* parse_type(TokenStream& src, int close,Node* owner) {
	auto tok=src.eat_tok();
	Type* ret=0;	// read the first, its the form..
	if (tok==close) return nullptr;
	if (tok==FN){	// fn(arg0,arg1,...)->ret
		ret=new Type(FN,src.pos);
		src.expect(OPEN_PAREN,"eg fn() fn name()");
		ret->push_back(parse_tuple(src,owner));// args
		parse_ret_val(src,owner,ret);
	}
	else if (tok==OR){ // closure |arg0,arg1,..|->ret
		ret = new Type(owner,CLOSURE);
		auto args=new Type(owner,TUPLE); ret->push_back(args);
		while ((src.peek_tok())!=OR){
			args->push_back(parse_type(src,OR,owner));
			src.eat_if(COMMA);
		}
		parse_ret_val(src,owner,ret);
	}
	else if (tok==OPEN_PAREN) {
		ret=parse_tuple(src,owner);
		if (src.eat_if(ARROW)){
			// tuple->type  defines a function.
			auto fn_ret=parse_type(src,0,owner);
			auto fn_type=new Type(owner,CLOSURE);
			fn_type->push_back(ret);
			fn_type->push_back(fn_ret);
			return fn_type;
		}
		
	} else {
		// prefixes in typegrammar..
		if (tok==MUL || tok==AND) {
			ret=new Type(owner,PTR);
			ret->sub=parse_type(src,close,owner);
		}else {
			// main: something[typeparams]..
			ret = new Type(owner,tok);
			if (src.eat_if(OPEN_BRACKET)) {
				while (auto sub=parse_type(src, CLOSE_BRACKET,owner)){
					ret->push_back(sub);
					src.eat_if(COMMA);
				}
			}
			// postfixes:  eg FOO|BAR|BAZ todo  FOO*BAR*BAZ   FOO&BAR&BAZ
			if (src.peek_tok()==OR && close!=OR){
				Type* sub=ret; ret=new Type(owner,VARIANT); ret->push_back(sub);
				while (src.eat_if(OR)){
					auto sub=parse_type(src,close,owner);
					ret->push_back(sub);
				}
			}
		}
	}
	if (!owner) ret->set_origin(ret);	// its a type declaration, 'origin is here'.
	// todo: pointers, adresses, arrays..
	return ret;
}
ArgDef* parse_arg(TokenStream& src, int close) {
	auto argname=src.eat_ident();
	if (argname==close) return nullptr;
	auto a=new ArgDef(src.pos,argname);
	a->pos=src.pos;
	if (src.eat_if(COLON)) {
		a->type()=parse_type(src,close,a);
	}
	if (src.eat_if(ASSIGN)){
		a->default_expr=parse_expr(src);
	}
	return a;
}
void parse_typeparams(TokenStream& src,vector<TypeParam*>& out) {
	while (!src.eat_if(CLOSE_BRACKET)){
		//		if (src.eat_if(CLOSE_BRACKET)) break;
		auto name=src.eat_tok();
		//		int d=0;
		//		if (src.eat_if(ASSIGN)) {
		//			int d=src.eat_tok();
		//		}
		out.push_back(new TypeParam{name,src.eat_if(ASSIGN)?parse_type(src,0,nullptr):0});
		src.eat_if(COMMA);
	}
}

ExprStructDef* parse_struct_body(TokenStream& src,SrcPos pos,Name name, Type* force_inherit);

ExprStructDef* parse_struct(TokenStream& src) {
	auto pos=src.pos;
	auto sname=src.eat_ident();
	return parse_struct_body(src,pos,sname,nullptr);
}
// TODO: are struct,trait,enum actually all the same thing with a different 'default'

ExprStructDef* parse_struct_body(TokenStream& src,SrcPos pos,Name name, Type* force_inherit){
	auto sd=new ExprStructDef(pos,name);
	// todo, namespace it FFS.
	if (src.eat_if(OPEN_BRACKET)) {
		parse_typeparams(src,sd->typeparams);
	}
	if (src.eat_if(COLON)) {
		sd->inherits_type = parse_type(src,0,nullptr); // inherited base has typeparams. only single-inheritance allowed. its essentially an anonymous field
	} else {
		sd->inherits_type = force_inherit; // enum is sugar rolling a number of classes from base
	}
	if (!src.eat_if(OPEN_BRACE))
		return sd;
	// todo: type-params.
	Name tok;
	while ((tok=src.peek_tok())!=NONE){
		if (tok==CLOSE_BRACE){src.eat_tok(); break;}
		if (src.eat_if(STRUCT)) {
			sd->structs.push_back(parse_struct(src));
		} else if (auto cmd=src.eat_if(FN)){
			sd->functions.push_back(parse_fn(src,sd));
		} else if (auto cmd=src.eat_if(VIRTUAL)){
			if (sd->inherits_type){
				error(sd,"limited vtables - currently this can only describe the vtable layout in the base class.\nTODO - this is just a temporary simplification,  other priorities eg rust-style traits, ADTs, static-virtuals, reflection..\n");
			}
			sd->virtual_functions.push_back(parse_fn(src,sd));
		} else if (auto cmd=src.eat_if(STATIC)){
			auto arg=parse_arg(src,CLOSE_PAREN);
			if (src.eat_if(VIRTUAL)){
				sd->static_virtual.push_back(arg);
			} else sd->static_fields.push_back(arg);
		} else {
			auto arg=parse_arg(src,CLOSE_PAREN);
			sd->fields.push_back(arg);
		}
		src.eat_if(COMMA); src.eat_if(SEMICOLON);
	}
	// if there's any virtual functions, stuff a vtable pointer in the start
	return sd;
}

ExprStructDef* parse_tuple_struct_body(TokenStream& src, SrcPos pos, Name name){
	Name tok;
	auto sd=new ExprStructDef(pos,name);
	if (src.eat_if(OPEN_BRACKET)) {
		parse_typeparams(src,sd->typeparams);
	}
	if (!src.eat_if(OPEN_PAREN))
		return sd;
	
	while ((tok=src.peek_tok())!=NONE){
		if (tok==CLOSE_PAREN){src.eat_tok(); break;}
		sd->fields.push_back(new ArgDef(pos,0,parse_type(src,0,sd)));
		src.eat_if(COMMA); src.eat_if(SEMICOLON);
	}
	return sd;
}

ExprStructDef* parse_enum(TokenStream& src) {
	auto pos=src.pos;
	auto tok=src.eat_ident();
	auto ed=new EnumDef(src.pos,tok);
	if (src.eat_if(OPEN_BRACKET)) {
		parse_typeparams(src,ed->typeparams);
	}
	if (src.eat_if(COLON)) {
		ed->inherits_type = parse_type(src,0,ed); // inherited base has typeparams. only single-inheritance allowed. its essentially an anonymous field
	}
	if (!src.eat_if(OPEN_BRACE))
		return ed;
	// todo: type-params.
	while ((tok=src.eat_tok())!=NONE){
		auto subpos=src.pos;
		if (tok==CLOSE_BRACE){break;}
		// got an ident, now what definition follows.. =value, {fields}, (types), ..
		if (src.peek_tok()==OPEN_BRACE){
			ed->structs.push_back(parse_struct_body(src,subpos,tok,nullptr));
		} else if (src.peek_tok()==OPEN_BRACKET){
			ed->structs.push_back(parse_tuple_struct_body(src,subpos,tok));
		} else if (src.eat_if(ASSIGN)){
			auto lit=parse_literal(src); lit->name=tok;
			ed->literals.push_back(lit);
		}
		src.eat_if(COMMA); src.eat_if(SEMICOLON);
	}
	return ed;
}

// iterator protocol. value.init. increment & end test.
ExprFor* parse_for(TokenStream& src){
	auto p=new ExprFor(src.pos);
	auto first=parse_block(src,SEMICOLON,COMMA,0);
	if (src.eat_if(IN)){
		p->pattern=first;
		p->init=parse_block(src, OPEN_BRACE, 0, 0);
		src.expect(OPEN_BRACE,"eg for x..in..{}");
	} else {//if (src.eat_if(SEMICOLON)){// cfor.  for init;condition;incr{body}
		p->pattern=0;
		p->init=first;
		p->cond=parse_block(src,SEMICOLON,COMMA,0);
		//ssrc.expect(SEMICOLON,"eg for init;cond;inc{..}");
		p->incr=parse_block(src,OPEN_BRACE,COMMA,0);
	}
 //else {
	//		error(p,"for..in.. or c style for loop, expect for init;cond;incr{body}");
	//	}
	p->body=parse_block(src, CLOSE_BRACE, SEMICOLON, nullptr);
	if (src.eat_if(ELSE)){
		src.expect(OPEN_BRACE,"after else");
		p->else_block=parse_block(src,CLOSE_BRACE, SEMICOLON, nullptr);
	}
	return p;
}

// make a flag for c or rust mode
// exact c parser
// add := gets rid of auto noise
// add postfix : alternate functoin syntax
ExprIf* parse_if(TokenStream& src){
	// TODO: assignments inside the 'if ..' should be in-scope
	// eg if (result,err)=do_something(),err==ok {....}  else {...}
	auto p=new ExprIf(src.pos);
	//	p->cond=parse_block(src, OPEN_BRACE, 0, 0);
	p->cond=parse_block(src,OPEN_BRACE,COMMA,0);
	p->body=parse_block(src, CLOSE_BRACE,SEMICOLON,0);
	verify(p->cond->get_type());
	
	if (src.eat_if(ELSE)) {
		if (src.eat_if(IF)) {
			p->else_block= parse_if(src);
		} else if (src.eat_if(OPEN_BRACE)){
			p->else_block=parse_block(src, CLOSE_BRACE, SEMICOLON, 0);
		} else {
			error(0,"if { }else {} expected\n");
		}
	}
	if (p->cond) verify(p->cond->get_type());
	if (p->body) verify(p->body->get_type());
	if (p->else_block) verify(p->else_block->get_type());
	return p;
}





