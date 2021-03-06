#include "exprstructdef.h"

size_t		ExprStructDef::alignment() const {
	size_t max_a=0; for (auto a:fields) max_a=std::max(max_a,a->alignment()); return max_a;
}
const Type*		ExprStructDef::get_elem_type(int i)const{
	if (i<0|| i>fields.size()){
		error_begin(this,"broken field acess %d/%d\n",i,fields.size());
		this->dump(0);
		error_end(this);
	}
	return this->fields[i]->type();
}
Name	ExprStructDef::get_elem_name(int i)const {
	return this->fields[i]->name;
}
void ExprStructDef::gather_symbols(Scope* outer_sc){
	auto sc=outer_sc->make_inner_scope(&this->scope,this,this);
	dbg_scope("adding scope to %s::%s=%p\n", outer_sc->name_str(), this->name_str(), this->scope);
	outer_sc->add_struct(this);

	for (auto f:static_fields) f->gather_symbols(this->scope);
	for (auto f:structs) f->gather_symbols(this->scope);
	for (auto f:typedefs) f->gather_symbols(this->scope);
	for (auto f:fields) f->gather_symbols(this->scope);
	for (auto f:functions) f->gather_symbols(this->scope);
	for (auto f:virtual_functions) f->gather_symbols(this->scope);
	for (auto f:static_functions) f->gather_symbols(this->scope);
	for (auto f:static_virtual) f->gather_symbols(this->scope);
	if (!this->constructor_wrappers.size()) this->roll_constructor_wrappers(this->scope);
 
}
int ExprStructDef::get_elem_index(Name name)const {
	int i;
	for (i=0; i<this->fields.size(); i++){
		if (this->fields[i]->name==name)
			return i;
	} return -1;
}

ExprStructDef*
ExprStructDef::get_common_base(ExprStructDef* e){
	for (auto x=this; x; x=x->inherits){
		if (e->has_base_class(x)){
			return x;
		}
	}
	return nullptr;
}


bool ExprStructDef::is_generic()const{
	if (tparams.size())
		return true;
	for (auto f:fields){if (!f->type())return true;}//TODO: is typeparam?
	return false;
}

bool
ExprStructDef::has_base_class(ExprStructDef* other)const{
	for (auto x=this; x;x=x->inherits)
		if (x==other)
			return true;
	return false;
}

int ExprStructDef::field_index(const Node* rhs){
	auto name=rhs->as_name();
	for (auto i=0; i<fields.size(); i++){
		if(fields[i]->name==name){
			((Node*)rhs)->set_def((ExprDef*)fields[i]);
			return i;
		}
	}
	return -1;
}


size_t ExprStructDef::size()const{
	size_t sum=0;
	for (auto i=0; i<fields.size();i++){
		sum+=fields[i]->size();
	}
	return sum;
}

Name ExprStructDef::get_mangled_name()const{
	if (!mangled_name){
		char buffer[1024];
		name_mangle(buffer,1024,this);
		const_cast<ExprStructDef*>(this)->mangled_name=getStringIndex(buffer);
	}
	return this->mangled_name;
}

ArgDef*	ExprStructDef::find_field(const Node* rhs)const{
	auto fi= this->try_find_field(rhs->as_name());
	if (!fi){
		error(rhs,this,"no field %s in %s",str(rhs->name),str(this->name));
	}

	return fi;
}
ArgDef* ExprStructDef::try_find_field(const Name fname)const{
	for (auto a:fields){
		if (a->name==fname)
			return a;
	}
	return nullptr;
}

void ExprStructDef::translate_tparams(const TParamXlat& tpx)
{
	this->instanced_types=tpx.given_types;

	for (auto a:this->fields)		a->translate_tparams(tpx);
	for (auto f:functions)			f->translate_tparams(tpx);
	for (auto f:virtual_functions)	f->translate_tparams(tpx);
	for (auto f:static_functions)	f->translate_tparams(tpx);
	for (auto f:static_fields)		f->translate_tparams(tpx);
	for (auto f:static_virtual)		f->translate_tparams(tpx);
	for (auto s:structs)			s->translate_tparams(tpx);
	((Node*)body)->translate_typeparams_if(tpx);
	if (tpx.typeparams_all_set())
		this->tparams.resize(0);
	this->type()->translate_typeparams_if(tpx);
	this->inherits_type->translate_typeparams_if(tpx);
	dbg(this->dump(0));
}
// TParamDef
Type* ExprStructDef::get_struct_type_for_tparams(const MyVec<TParamVal *> &given_types){
	// todo: actually scan the instances, as it might already exist.
	auto st=(Type*)new Type(this->pos,this->name);
	int i=0;
	for (auto tp:given_types){
		if (tp){
			st->push_back((Type*)tp->clone());
		} else {
			st->push_back(this->tparams[i]->default_or_auto());
		}
		i++;
	}
	dbg_tparams({dbprintf("\nget_t=");st->dump(0);dbprintf("\n");});
	return st;
}
index_t get_tparam_index(const Vec<TParamDef*>& tparams, const Node* n){
	for (index_t i=0; i<tparams.size(); i++){
		if (tparams[i]->name==n->name)
			return i;
	}
	return -1;
}
ExprStructDef*	get_base_instance(Scope* sc, const Vec<TParamDef*>& context_tparams, const Type* context_type, Type* desired){
	// translate a base class or method 'member' for a mix of tparams & given types.
	Vec<TParamVal*> desired_tparams;
//  eg
	dbg_tparams({dbprintf("\ndesired=");desired->dump(0);});
	dbg_tparams({dbprintf("\ncontext=");context_type->dump(0);});
	dbprintf_tparams("\nfind param list..\n");
	for (auto subt=desired->sub; subt; subt=subt->next){
		auto i=get_tparam_index(context_tparams, subt);
		if (i>=0){
			auto dt=(TParamVal*)context_type->get_type_sub(i);
			dbg_tparams({dt->dump(0);dbprintf("\n");});
			desired_tparams.push_back(dt);
		} else {
			desired_tparams.push_back((TParamVal*)subt->clone());
		}
	}
	auto sd=sc->find_struct_named(desired->name);
	dbg_tparams({dump_tparams(sd->tparams,&desired_tparams);dbprintf("\n");});
	return sd->get_instance(sc, sd->get_struct_type_for_tparams(desired_tparams));
}
ExprStructDef* ExprStructDef::get_struct_named(Name n){
	for (auto s:structs){
		if (s->name==n) return s;
	}
	return nullptr;
}
ExprStructDef* ExprStructDef::find_instance(Scope* sc,const Type* type){
	// scan the heirachy, because of nested structs. (parent -> child -> instances
	if (this->owner){
		auto p=this->owner->get_struct_named(this->name);
		if (auto si=p->find_instance_sub(sc,type))
			return si;

		for (auto oi=this->owner->instances; oi;oi=oi->next_instance){
			auto p=oi->get_struct_named(this->name);
			if (auto si=p->find_instance_sub(sc,type))
				return si;
		}
	}
	if (this->inherits && this->m_is_variant){
		auto p=this->inherits->get_struct_named(this->name);
		if (auto si=p->find_instance_sub(sc,type))
			return si;
		
		for (auto oi=this->inherits->instances; oi;oi=oi->next_instance){
			auto p=oi->get_struct_named(this->name);
			if (auto si=p->find_instance_sub(sc,type))
				return si;
		}
	}

//	for (auto oi=this->instance_of; oi;oi=oi->next_instance){
//		auto p=oi->get_struct_named(this->name);
//		if (auto si=p->find_instance_sub(sc,type))
//			return si;
	if (auto r=find_instance_sub(sc,type))
		return r;
	return nullptr;
}

ExprStructDef* ExprStructDef::find_instance_sub(Scope* sc,const Type* type){
	for (auto ins=this->instances;ins; ins=ins->next_instance) {
		// instances may themselves be in a tree? eg specialize one type, store the variations of others..
		if (auto si=ins->find_instance_sub(sc,type))
			return si;
		if (type_params_eq(ins->instanced_types,type->sub))
			return ins;
	}
	return nullptr;
}

ExprStructDef* ExprStructDef::get_instance(Scope* sc, const Type* type) {
	auto parent=this;
	if (!this->is_generic())
		return this;
	// first things first, if it inherits something - we need its' base class instanced too.
	// if this was a component (enum..) this will also create component instances
	ExprStructDef* parent_sd=nullptr;
	if (this->inherits_type){
		parent_sd=get_base_instance(sc,this->tparams, type,this->inherits_type);
		dbg_tparams(parent_sd->dump(0));
		if (this->m_is_variant){
			// where to put the new instance, if its' a component of a enum.
			// the owner gets instanced first.
			auto parallel_this=parent_sd->get_struct_named(this->name);
			if (parallel_this!=this)
				return parallel_this->get_instance(sc,type);
		}
	}
	auto ins=this->find_instance(sc,type);
	if (!ins) {
#if DEBUG>=2
		dbg_instancing("instantiating struct %s<",this->name_str());
		for (auto t=type->sub;t;t=t->next)dbg_instancing("%s,",t->name_str());
		dbg_instancing(">\n");
		dbg_instancing("%s now has %d instances\n",this->name_str(),this->num_instances()+1);
		dbg_instancing("owner=%p inherits=%p original=%p\n",this->owner,this->inherits,this);
#endif
		// TODO: store a tree of partial instantiations eg by each type..
		MyVec<Type*> ty_params;
		int i=0;
		Type* tp=type->sub;
		for (i=0; i<parent->tparams.size() && tp; i++,tp=tp->next){
			ty_params.push_back(tp);
		}
		for (;i<parent->tparams.size(); i++) {
			ty_params.push_back(parent->tparams[i]->defaultv);
		}
		
		ins = (ExprStructDef*)this->clone(); // todo: Clone could take tparams
							// cloning is usually for template instantiation?
//		ins->inherits = parent_sd;
		ins->instance_of=this;
		ins->next_instance = this->instances; this->instances=ins;
		ins->inherits_type= this->inherits_type; // TODO: tparams! map 'parent' within context  to make new typeparam vector, and get an instance for that too.
		dbg_tparams({for (auto i=0; i<ins->instanced_types.size();i++)
			dbprintf(ins->instanced_types[i]->name_str());});
		ins->translate_tparams(TParamXlat(this->tparams, ty_params));
		if (ins->inherits){
			ins->inherits=ins->inherits->get_instance(this->scope,this->inherits_type);
			dbg_tparams(ins->inherits->dump(0));
		}
		if (this->m_is_variant){
			this->owner=this->inherits;
		}

		dbg(printf("instances are now:-\n"));
		dbg(this->dump_instances(0));
	}
	if (!type->struct_def()) { const_cast<Type*>(type)->set_struct_def(ins);}
//	else { ASSERT(type->struct_def==ins && "instantiated type should be unique")};
	
	return ins;
}
void ExprStructDef::dump_instances(int depth)const{
	if (!this)return ;
	int x=0;
	newline(depth);	dbprintf("instances of %s{",this->name_str());
	for (auto i=this->instances;i;i=i->next_instance,x++){
		dbprintf("\n/*%s instance %d/%d*/",this->name_str(), x,this->num_instances());
		i->dump(depth+1);
	}
	newline(depth);	dbprintf("}",this->name_str());
}

Node* ExprStructDef::clone() const{
	return this->clone_sub(new ExprStructDef(this->pos,this->name));
}
void ExprStructDef::recurse(std::function<void (Node *)> & f){
	if(this) return;
	for (auto a:this->args)		a->recurse(f);
	for (auto x:this->fields)		x->recurse(f);
	for (auto x:this->tparams)	x->recurse(f);
	for (auto x:this->functions)	x->recurse(f);
	for (auto x:this->virtual_functions)x->recurse(f);
	for (auto x:this->static_functions)x->recurse(f);
	for (auto x:this->static_fields)x->recurse(f);
	for (auto x:this->static_virtual)x->recurse(f);
	for (auto x:this->structs)x->recurse(f);
	for (auto x:this->literals)x->recurse(f);
	for (auto x:this->typedefs)x->recurse(f);
	((Node*)this->body)->recurse(f);
	this->type()->recurse(f);
}

Node* ExprStructDef::clone_sub(ExprStructDef* d)const {
	d->discriminant=discriminant;
	d->m_is_enum=m_is_enum;
	d->m_is_variant=m_is_variant;
	for (auto tp:this->tparams){auto ntp=(TParamDef*)tp->clone();
		d->tparams.push_back(ntp);}
	for (auto a:this->args) {d->args.push_back((ArgDef*)a->clone());}
	for (auto m:this->fields) {d->fields.push_back((ArgDef*)m->clone());}
	for (auto f:this->functions){d->functions.push_back((ExprFnDef*)f->clone());}
	for (auto f:this->virtual_functions){d->virtual_functions.push_back((ExprFnDef*)f->clone());}
	for (auto f:this->static_functions){d->static_functions.push_back((ExprFnDef*)f->clone());}
	for (auto f:this->static_fields){d->static_fields.push_back((ArgDef*)f->clone());}
	for (auto f:this->static_virtual){d->static_virtual.push_back((ArgDef*)f->clone());}
	for (auto s:this->structs){d->structs.push_back((ExprStructDef*)s->clone());s->owner=d;}
	for (auto l:this->literals){d->literals.push_back((ExprLiteral*)l->clone());}
	for (auto l:this->typedefs){d->typedefs.push_back((TypeDef*)l->clone());}
	d->inherits=this->inherits;
	d->inherits_type=(Type*)this->inherits_type->clone_if();
	d->body=(ExprBlock*)((Node*)this->body)->clone_if();
	return d;
}

void ExprStructDef::inherit_from(Scope * sc,Type *base_type){
	if (inherits!=0) return;// already resolved.ø
	auto base_template=sc->find_struct_named(base_type->name);
	ExprStructDef* base_instance=base_template;
	if (base_type->is_template()) {
		base_instance = base_template->get_instance(sc, base_type);
	}
	ASSERT(inherits==0); next_of_inherits=base_instance->derived; base_instance->derived=this; this->inherits=base_instance;
}

ExprStructDef* ExprStructDef::root_class(){
	for (auto s=this; s;s=s->inherits){
		if (!s->inherits)
			return s;
	}
	return this;
}

bool ExprStructDef::roll_vtable() {
	
	if (this->is_vtable_built()){// ToDO: && is-class. and differentiate virtual functions. For the
		return true;
	}
	for (auto f:this->virtual_functions){// can't build it yet.
		if (!f->fn_type)
			return false;
	}

	dbg_vtable("rolling vtable for %s,inherits %s, scope=%p\n",str(this->name),this->inherits?str(this->inherits->name):"void", this->scope);
	if (this->vtable){
		return true;} // done already
	if (this->inherits) {
		auto ps=this->inherits->get_scope();
		if (!ps) {
			this->inherits->dump(0);
			return false;
		}
		if (this->inherits->has_vtable() && !this->inherits->vtable)
			this->inherits->resolve(ps->parent_or_global(),nullptr,0);
		auto r=this->inherits->roll_vtable();
		if (!r)
			return false;
	}

	this->vtable_name=getStringIndexConcat(name, "__vtable_instance");

	// todo - it should be namespaced..
	//this->vtable->scope=this->scope;

	// todo: we will create a global for the vtable
	// we want to be able to emulate rust trait-objects
	// & hotswap vtables at runtime for statemachines

	// todo: this is a simplification - only the class root describes the vtable.
	auto root=this->root_class();
	if (root!=this){
		this->vtable=root->vtable;
	}
	else{
		if (!this->virtual_functions.size())
			return true;
		this->vtable=new ExprStructDef(this->pos,getStringIndexConcat(name,"__vtable_format"));
		this->vtable->vtable_name=getStringIndex("void__vtable");

		for (auto f:this->virtual_functions) {
			// todo: static-virtual fields go here!
			auto fnt=(Type*)(f->fn_type->clone());
			
			if (fnt->sub){
				if(fnt->sub->sub)
					if (fnt->sub->sub->sub)
						fnt->sub->sub->sub->replace_name_if(this->name,SELF_T);
				if (fnt->sub->next)
					if (fnt->sub->next->next)
						fnt->sub->next->next->replace_name_if(this->name,SELF_T);
			}
			this->vtable->fields.push_back(
					new ArgDef(
					this->pos,
					f->name,
					fnt,
					new ExprIdent(this->pos, f->name)
				)
			);
		}
		for (auto svf:static_virtual){
			this->vtable->fields.push_back(svf);
		}
	}
	// base class gets a vtable pointer
	if (this->vtable){
		this->fields.insert(0,
//			this->fields.begin(),
			new ArgDef(pos,__VTABLE_PTR,new Type(PTR,this->vtable)));
	}

	// TODO - more metadata to come here. struct layout; pointers,message-map,'isa'??
	return this->vtable;
}

void ExprStructDef::set_variant_of(ExprStructDef* owner, int index){
	set_discriminant(index);
	ASSERT(inherits==0);
	inherits=owner;
	inherits_type=owner->get_struct_type();
//	new Type(this->pos, owner->name);
}


void ExprStructDef::dump(PrinterRef depth) const{
	auto depth2=depth>=0?depth+1:depth;
	newline(depth);
	dbprintf("<%s> %s",this->kind_str(), getString(this->name));
	dump_tparams(this->tparams,&this->instanced_types);
	if (this->inherits_type) {dbprintf(":"); this->inherits_type->dump_if(-1);}
	if (this->args.size()){
		dbprintf("(");for (auto a:this->args)	{a->dump(-1);dbprintf(",");};dbprintf(")");
	}
	dbprintf("{");
	dump_struct_body(depth);
	newline(depth);dbprintf("}");
	if (this->body){
		dbprintf("where");
		((Node*)this->body)->dump(depth);
	}
	newline(depth);dbprintf("</%s>",this->kind_str());
}
void ExprStructDef::dump_struct_body(int depth)const {
	auto depth2=depth>=0?depth+1:depth;
	if (this->m_is_variant){
		newline(depth2);dbprintf("__discriminant=%d, ",this->discriminant);
	}
	for (auto m:this->literals)	{m->dump(depth2);}
	for (auto m:this->fields)	{m->dump(depth2);}
	for (auto s:this->structs)	{s->dump(depth2);}
	for (auto f:this->functions){f->dump(depth2);}
	for (auto f:this->virtual_functions){f->dump(depth2);}
}
int ExprStructDef::first_user_field_index() const{
	int i=0;
	while (i<this->fields.size()){
		if (this->fields[i]->name!=__VTABLE_PTR && this->fields[i]->name!=__DISCRIMINANT){
			return i;
		}
		i++;
	}
	return i;
}

void ExprStructDef::calc_trailing_padding(){
	auto maxsize=0;
	auto thissize=size();
	if (this->inherits && this->m_is_variant){
		if (thissize>this->inherits->max_variant_size){
			this->inherits->max_variant_size=thissize;
		}
	}
 
	for (auto ins=this->instances; ins; ins=ins->next_instance){
		ins->calc_trailing_padding();
	}
	for (auto s:structs){
		s->calc_trailing_padding();
		auto sz=s->size();
		if (sz > maxsize)
			maxsize=sz;
//		for (auto ss=s->instances; ss;ss=ss->next_instance){
//			s->calc_trailing_padding();
//			auto sz=s->size();
//			if (sz > maxsize)
//				maxsize=sz;
//		}
	}
	this->max_variant_size=maxsize;
}
size_t ExprStructDef::padding()const{
	int r=0;
	if (this->m_is_enum){
		r= max_variant_size-size();
	}
	if (this->m_is_variant && inherits){
		r= inherits->max_variant_size-size();
	}
	if (r<0){
		error_begin(this,"padding calcualtion broken\n");
		this->inherits->dump(0);
		dbprintf("\n");
		this->dump(0);
		dbprintf("parent type:-\n");
		this->inherits_type->dump(0);
		dbprintf("maxsize=%d this size=%d pad=%d\n",inherits->max_variant_size, this->size(),r);
		error_end(this);
	}
	return r;
}


void
ExprStructDef::roll_constructor_wrappers(Scope* sc){
	// constructors are methods taking a this pointer.
	// constructor-wrappers are functions with the same name as the type in the above scope,
	// which instantiate a struct, invoke the constructor, and return the struct.
	for (auto srcf:this->functions) {
		if (srcf->name!=this->name) continue;
		auto fpos=srcf->pos;
		auto nf=new ExprFnDef(fpos);
		nf->name=this->name;
		auto fbody = new ExprBlock(fpos);
		nf->body=fbody;
		auto varname=getStringIndex("temp_object");
		auto si=new ExprStructInit(fpos, new ExprIdent(fpos, this->name) ); // todo - tparams
		si->call_expr->set_def(this);
		auto l=new ExprOp(LET_ASSIGN,fpos, (Expr*)new Pattern(fpos,varname),si);
		auto call=new ExprCall();call->pos=fpos;
		call->call_expr=new ExprIdent(fpos,this->name);
		call->call_expr->set_def(srcf);
		call->argls.push_back(new ExprOp(ADDR,fpos, nullptr, new ExprIdent(fpos,varname)));
		for (int i=1; i<srcf->args.size();i++){
			auto a=srcf->args[i];
			nf->args.push_back((ArgDef*)a->clone());
			call->argls.push_back(new ExprIdent(fpos,a->name));
		}
		fbody->argls.push_back(l);
		fbody->argls.push_back(call);
		fbody->argls.push_back(new ExprIdent(fpos,varname));
		
		sc->parent_or_global()->add_fn(nf);
		this->constructor_wrappers.push_back(nf);
		nf->dump(0);
		nf->resolve_if(sc,nullptr,0);
		//		call->call_expr->set_def(srcf);
		//		call->call_expr->clear_type();
		//		call->call_expr->set_type(srcf->fn_type);
		nf->dump(0);
	}
}

// this needs to be called on a type, since it might be a tuple..
// oh and what about enums!
bool ExprStructDef::has_constructors()const{
	auto sd=this;
	if (!sd) return false;
	for (auto f:sd->functions){
		if (f->name==sd->name)
			return true;
	}
	return has_sub_constructors();
}
bool ExprStructDef::has_sub_constructors()const{
	for (auto f:fields){
		if (!f->get_type()) continue;
		if (auto sd=f->type()->struct_def()->has_constructors()){
			return true;
		}
	}
	if (this->inherits) if (this->inherits->has_sub_constructors()) return true;
	return this->inherits_type?this->inherits_type->has_sub_constructors():false;
}
bool ExprStructDef::has_sub_destructors()const{
	for (auto f:functions){
		if (f->name==__DESTRUCTOR){
//			dbg_raii(f->dump(0));dbg_raii(newline(0));
			return true;
		}
	}
	for (auto f:fields){
		if (!f->get_type()) continue;
		if (auto sd=f->type()->has_sub_destructors()){
			return true;
		}
	}
	if (this->inherits) if (this->inherits->has_sub_destructors()) return true;
	return this->inherits_type?this->inherits_type->has_sub_destructors():false;
}

void	ExprStructDef::insert_sub_constructor_calls(){
	if (m_ctor_composed) return;
	if (!this->has_sub_constructors())
		return;
	get_or_create_constructor();
	m_ctor_composed=true;
	for (auto f:functions){
		if (f->name!=this->name)
			continue;
		dbg_raii(printf("inserting component constructors on %s\n",this->name_str()));
		dbg_raii(f->dump(0));
		insert_sub_constructor_calls_sub(f);
		dbg_raii(f->dump(0));
	}
}
void	ExprStructDef::ensure_constructors_return_thisptr(){
	for (auto f:functions){
		if (f->name!=this->name)
			continue;
		if (auto ls=f->get_return_expr()){
			if (auto id=ls->as_ident())
				if (id->name==THIS)
					continue;
		}
		f->clear_return_type();// another resolve  pass will fill it in.
		dbg_raii(f->dump(0));
		f->convert_body_to_compound();
		f->push_body_back(new ExprIdent(f->pos,THIS));
		dbg_raii(f->dump(0));
	}
}


void		ExprStructDef::insert_sub_constructor_calls_sub(ExprFnDef* ctor){
	if (this->inherits)
		this->inherits->insert_sub_constructor_calls_sub(ctor);
	for (auto f:fields){
		if (f->type()->struct_def()->has_constructors()){
			if (auto sd=f->type()->struct_def()){
				auto subd=sd->get_or_create_constructor();
				auto pos=ctor->pos;
				ctor->push_body_front(new ExprCall(ctor->pos,subd,
//												   new ExprOp(ADDR,ctor->pos,nullptr,
															  new ExprOp(DOT,ctor->pos, new ExprIdent(ctor->pos,THIS), new ExprIdent(ctor->pos,f->name))));
			} else{
				error(this,"can't constructor yet");
			}
		}
	}
}
void		ExprStructDef::insert_sub_destructor_calls(ExprFnDef* dtor){
	if (m_dtor_composed) return;
	for (auto f:fields){
		if (f->type()->has_sub_destructors()){
			if (auto sd=f->type()->struct_def()){
				auto subd=sd->get_or_create_destructor();
				dbg_raii(printf("%s using %s::~%s()\n",str(this->name),str(sd->name),str(subd->name)));
				auto pos=dtor->pos;
//				dtor->push_body_back(new ExprCall(dtor->pos,subd->name,new ExprOp(ADDR,dtor->pos,nullptr,new ExprOp(DOT,dtor->pos, new ExprIdent(dtor->pos,THIS), new ExprIdent(dtor->pos,f->name)))));
				dtor->push_body_back(
					new ExprOp(DOT,pos,
						//new ExprOp(ADDR,pos, nullptr, new ExprOp(DOT,pos, new ExprIdent(pos,THIS),new ExprIdent(pos,f->name))),
						new ExprOp(DOT,pos, new ExprIdent(pos,THIS),new ExprIdent(pos,f->name)),
						new ExprCall(pos, subd->name))
				);
				dbg_raii(f->dump(0));
				dbg_raii(dtor->dump(0));
				m_dtor_composed=true;
			} else{
				error(this,"can't make destructor yet");
			}
		}
	}
	if (this->inherits)
		this->inherits->insert_sub_destructor_calls(dtor);
}
ExprFnDef* create_method(ExprStructDef* s,Name nm){
	dbg_raii(dbprintf("creating method %s::%s()\n", s->name_str(),str(nm)));
	auto f=new ExprFnDef(s->pos, nm);
	f->set_receiver_if_unset(s);
	f->args.push_back(new ArgDef(s->pos, THIS,s->ptr_type));
	s->functions.push_back(f);
	return f;
}
ExprFnDef*	ExprStructDef::get_or_create_destructor(){
	for (auto f:functions){
		if (f->name==__DESTRUCTOR)
			return f;
	}
	return create_method(this,__DESTRUCTOR);
}
ExprFnDef*	ExprStructDef::get_or_create_constructor(){
	// get which constructor?.. the default
	for (auto f:functions){
		if (f->name==this->name && (f->args.size()==1||(f->m_receiver && f->args.size()==0)))
			return f;
	}
	return create_method(this,this->name);
}

void ExprStructDef::init_types(){
	if (!this->struct_type){
		auto st=this->struct_type=new Type(this->pos,this);
		for (auto i=0; i<this->tparams.size(); i++){
			auto tp=this->tparams[i];
			if (i<this->instanced_types.size()){
				st->push_back((TParamVal*)(this->instanced_types[i]->clone_or_auto()));
			}else{
				st->push_back(new Type(this->pos, tp->name));
			}
		}
		this->ptr_type=new Type(this,PTR, this->struct_type);
		this->ref_type=new Type(this,REF, this->struct_type);
	}
}
Type* ExprStructDef::get_struct_type(){
	init_types();
	return this->struct_type;
}
void combine_tparams(MyVec<TParamDef*>* dst,const MyVec<TParamDef*>* src){
	if (src && dst){
		for (auto t:*src)
			dst->push_back((TParamDef*)t->clone());
	}
}
bool ExprStructDef::is_base_known()const{
	if (!this->inherits_type) return true;	// known to be null.
	if (this->inherits) return true;
	return false;
}
void ExprStructDef::set_owner_pointers(){
	for (auto s:structs){s->owner=this;s->set_owner_pointers();}
//	for (auto f:functions){f->owner=this;}
//	for (auto m:constructor_wrappers)	{m->owner=this;}

}
void
ExprStructDef::setup_enum_variant(){
	if (this->m_is_variant || this->m_is_enum){
		if (!this->fields.size()||this->fields.front()->name!=__DISCRIMINANT){
			this->fields.insert(
								0,//this->fields.begin(),
								new ArgDef(pos,__DISCRIMINANT,new Type(this->pos,I32)));
			dbg2(this->dump(0));
		}
		if (this->m_is_enum)
			calc_trailing_padding();
	}
}

ResolveResult ExprStructDef::resolve(Scope* definer_scope,const Type* desired,int flags){
	setup_enum_variant();
	if (m_recurse) return COMPLETE;	// TODO - this seems to have caused bugs, can we eliminate m_recures? it was added to stop infinite loop on constructor fixup.
	m_recurse=true;
	//definer_scope->add_struct(this);
	if (!this->get_type()) {
		this->set_type(new Type(this,this->name));	// name selects this struct
	}
	init_types();
	auto sc=definer_scope->make_inner_scope(&this->scope,this,this);
	ensure_constructors_return_thisptr();	// makes rolling wrappers easier.

	set_owner_pointers();
	if (!this->m_symbols_added){

		if (this->is_base_known()){
			if (this->inherits)
				combine_tparams(&this->tparams,&this->inherits->tparams);
			m_symbols_added=true;
			for (auto s:structs){
				if (s->inherits!=this)
					combine_tparams(&s->tparams,&this->tparams);
				resolved|=s->resolve_if(sc,nullptr,flags);
			}
			for (auto f:functions){
				combine_tparams(&f->tparams,&this->tparams);
				resolved|=f->resolve_if(sc,nullptr,flags);
			}
			
			dbg_tparams({this->dump(0);newline(0);});
		}
	}

	if (!this->is_generic()){
		this->m_fixup=true;
		// ctor/dtor composition,fixup.
		this->insert_sub_constructor_calls();
		if (this->has_sub_destructors()){
			this->insert_sub_destructor_calls(this->get_or_create_destructor());
		}

		for (auto m:fields)			{resolved|=m->resolve_if(sc,nullptr,flags&~R_FINAL);}
		for (auto m:static_fields)	{resolved|=m->resolve_if(sc,nullptr,flags);}
		for (auto m:static_virtual)	{resolved|=m->resolve_if(sc,nullptr,flags);}
		for (auto s:structs){
			resolved|=s->resolve_if(sc,nullptr,flags);
		}
		for (auto f:functions){
			resolved|=f->resolve_if(sc,nullptr,flags);
		}
		for (auto f:virtual_functions){
			resolved|=f->resolve_if(sc,nullptr,flags);
		}
		if (!this->constructor_wrappers.size()) this->roll_constructor_wrappers(sc);
		for (auto m:constructor_wrappers)	{resolved|=m->resolve_if(sc,nullptr,flags);}
		
		if (this->inherits_type && !this->inherits){
			resolved|=this->inherits_type->resolve_if(definer_scope,desired,flags);
			this->inherits=definer_scope->find_struct_named(this->inherits_type->name);
		}
		
		roll_vtable();
		/// TODO clarify that we dont resolve a vtable.
		//if (this->vtable) this->vtable->resolve(definer_scope,desired,flags);
		
	} else{
		for (auto ins=this->instances; ins; ins=ins->next_instance)
			resolved|=ins->resolve_if(definer_scope,nullptr, flags);
	}

	m_recurse=false;
	return propogate_type_fwd(flags,this, desired);
}


Node* EnumDef::clone()const {
	return this->clone_sub(new EnumDef(this->pos,this->name));
}


void compile_vtable_data(CodeGen& cg, ExprStructDef* sd, Scope* sc,ExprStructDef* vtable_layout){
	// compile formatted vtable with additional data..
	if (!vtable_layout->is_compiled){
		// the vtable really is just a struct; eventually a macro system could generate
		vtable_layout->compile(cg,sc,CgValue());
		vtable_layout->is_compiled=true;
	}
	dbg_vtable("compiling vtable for %s\n",sd->name_str());
	cg.emit_global_begin(sd->vtable_name);
	cg.emit_typename(str(vtable_layout->mangled_name));
	cg.emit_struct_begin(16);
	
	for (auto a:vtable_layout->fields){
		auto* s=sd;
		for (;s;s=s->inherits){
			if (auto f=s->find_function_for_vtable(a->name, a->type())){
				cg.emit_fn_cast_global(f->get_mangled_name(),f->fn_type,a->type());
				break;
			}
		}
		if (!s){
			cg.emit_undef();
		}
	}
	
	cg.emit_struct_end();
	cg.emit_ins_end();
}

CgValue ExprStructDef::compile(CodeGen& cg, Scope* sc, CgValue input) {
	if (this->is_compiled) return CgValue();
	this->is_compiled=true;
	calc_trailing_padding();
	if (this->m_is_variant && !this->owner){
		return CgValue();
	}
	auto st=this;
	// instantiate the vtable
	// todo: step back thru the hrc to find overrides
	if (this->vtable)
		compile_vtable_data(cg, this,sc, this->vtable);
	// compile inner structs. eg struct Scene{struct Mesh,..} struct Scene uses Mesh..
	for( auto sub:st->structs){
		sub->compile(cg,sc,input);
	}
	int i=0;
	for (auto ins=st->instances; ins; ins=ins->next_instance,i++){
		cg.emit_comment("instance %d: %s %s in %s %p",i,str(st->name),str(ins->name) ,sc->name_str(),ins);
		ins->get_mangled_name();
		for (auto i0=st->instances;i0!=ins; i0=i0->next_instance){
			if (i0->mangled_name==ins->mangled_name){
				cg.emit_comment("ERROR DUPLICATE INSTANCE this shouldn't happen, its a bug from inference during struct initializers (starts with a uncertain instance, then eventually fills in tparams - not quite sure how best to fix it right now\n");
				goto cont;
			}
		}
		ins->compile(cg, sc,input);
	cont:;
	}
/*
	if (st->is_generic()&&0) {	// emit generic struct instances
		cg.emit_comment("instances of %s in %s %p",str(st->name), sc->name(),st);
		int i=0;
		dbg(this->dump_instances(0));
		
	} else
*/
	if (this->instanced_types.size()>=this->tparams.size())
	{
		cg.emit_comment("%s<%s>",str(st->name),st->instanced_types.size()?str(st->instanced_types[0]->name):st->tparams.size()?str(st->tparams[0]->name):"");
		cg.emit_comment("instance %s of %s in %s\t%p.%p.%p",str(st->name),st->instance_of?st->instance_of->name_str():"none" ,sc->name_str(),st->owner,st->inherits, st);
		
		for (auto fi: st->fields){
			if (!fi->type())
				return CgValue();
			if (fi->type()->is_typeparam(sc))
				return CgValue();
			if (fi->type()->name>=IDENT){
				if (!fi->type()->struct_def()){
					cg.emit_comment("not compiling %s, it shouldn't have been instanced-see issue of partially resolving generic functions for better type-inference, we're getting these bugs: phantom initiated structs. must figure out how to mark them properly",str(this->get_mangled_name()));
					return CgValue();
				}
			}
		};
		
		cg.emit_struct_def_begin(st->get_mangled_name());
		for (auto fi: st->fields){
			cg.emit_type(fi->type(), false);
		};
		if (auto pad=st->padding()){
			dbprintf("%d\n",pad);
			cg.emit_array_type(Type::get_u8(),pad);
		}

		cg.emit_struct_def_end();
		
	}
	return CgValue();	// todo: could return symbol? or its' constructor-function?
}
Node* TraitDef::clone()const{
	auto td=new TraitDef(this->pos, this->name);
	return this->clone_sub(td);
}
ResolveResult	TraitDef::resolve(Scope* scope, const Type* desired,int flags){
	dbprintf("warning traits not yet implemented");
	return COMPLETE;
}
CgValue TraitDef::compile(CodeGen& cg, Scope* sc,CgValue input) {
	dbprintf("warning traits not yet implemented");
	return CgValue();
}

Node* ImplDef::clone()const{
	auto imp=new ImplDef(this->pos, this->name);
	imp->impl_trait = this->impl_trait;
	imp->impl_for_type = this->impl_for_type;
	return this->clone_sub(imp);
}
void ImplDef::dump(PrinterRef depth) const{
	
	newline(depth);
	dbprintf("%s ",this->kind_str());dump_tparams(this->tparams,&this->instanced_types);
	if (this->impl_trait) {this->impl_trait->dump_if(-1);dbprintf(" for ");}
	if (this->impl_for_type) this->impl_for_type->dump_if(-1);
	
	dump_struct_body(depth);
	newline(depth);dbprintf("}");
}
Node* VariantDef::clone()const{
	return this->clone_sub(new VariantDef(this->pos, this->name));
}


//CgValue EnumDef::compile(CodeGen &cg, Scope *sc){
//	return CgValue();
//}






