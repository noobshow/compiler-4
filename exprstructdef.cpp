#include "semantics.h"
#include "codegen.h"
#include "repl.h"
#include "lexer.h"
#include "parser.h"
#include "run_test.h"
#include "error.h"
#include "exprfndef.h"
#include "exprstructdef.h"

bool ExprStructDef::is_generic()const{
	if (typeparams.size())
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
	if (!fi)
		error(rhs,this,"no field %s in ",str(name),str(this->name));

	return fi;
}
ArgDef* ExprStructDef::try_find_field(const Name name)const{
	for (auto a:fields){if (a->name==name) return a;}
	return nullptr;
}

void ExprStructDef::translate_typeparams(const TypeParamXlat& tpx)
{
	for (auto a:this->fields)		a->translate_typeparams(tpx);
	for (auto f:functions)			f->translate_typeparams(tpx);
	for (auto f:virtual_functions)	f->translate_typeparams(tpx);
	for (auto f:static_functions)	f->translate_typeparams(tpx);
	for (auto f:static_fields)		f->translate_typeparams(tpx);
	for (auto f:static_virtual)		f->translate_typeparams(tpx);
	for (auto s:structs)			s->translate_typeparams(tpx);
	if (tpx.typeparams_all_set())
		this->typeparams.resize(0);
	this->type()->translate_typeparams_if(tpx);
}

ExprStructDef* ExprStructDef::get_instance(Scope* sc, const Type* type) {
	auto parent=this;
	if (!this->is_generic())
		return this;
	// make the typeparams..
	// search for existing instance
	ExprStructDef* ins=parent->instances;
	for (;ins; ins=ins->next_instance) {
		if (type_params_eq(ins->instanced_types,type->sub))
			break;
	}
	if (!ins) {
#if DEBUG>=2
		dbg_instancing("instantiating struct %s[",this->name_str());
		for (auto t=type->sub;t;t=t->next)dbg_instancing("%s,",t->name_str());
		dbg_instancing("]\n");
#endif
		// TODO: store a tree of partial instantiations eg by each type..
		vector<Type*> ty_params;
		int i=0;
		Type* tp=type->sub;
		for (i=0; i<parent->typeparams.size() && tp; i++,tp=tp->next){
			ty_params.push_back(tp);
		}
		for (;i<parent->typeparams.size(); i++) {
			ty_params.push_back(parent->typeparams[i]->defaultv);
		}
		
		ins = (ExprStructDef*)this->clone(); // todo: Clone could take typeparams
							// cloning is usually for template instantiation?
		ins->instanced_types=ty_params;
		ins->instance_of=this;
		ins->next_instance = this->instances; this->instances=ins;
		ins->inherits_type= this->inherits_type; // TODO: typeparams! map 'parent' within context  to make new typeparam vector, and get an instance for that too.
		if (g_debug_get_instance)
			for (auto i=0; i<ins->instanced_types.size();i++)
				dbprintf(ins->instanced_types[i]->name_str());
		ins->translate_typeparams(TypeParamXlat(this->typeparams, ins->instanced_types));
	}
	if (!type->struct_def()) { const_cast<Type*>(type)->set_struct_def(ins);}
//	else { ASSERT(type->struct_def==ins && "instantiated type should be unique")};
	
	return ins;
}

Node* ExprStructDef::clone() const{
	return this->clone_sub(new ExprStructDef(this->pos,this->name));
}
Node* ExprStructDef::clone_sub(ExprStructDef* d)const {
	for (auto m:this->fields) {d->fields.push_back((ArgDef*)m->clone());}
//	for (auto t:this->typeparams) {d->typeparams.push_back(t->clone());}
	d->typeparams = this->typeparams;
	for (auto f:this->functions){d->functions.push_back((ExprFnDef*)f->clone());}
	for (auto f:this->virtual_functions){d->virtual_functions.push_back((ExprFnDef*)f->clone());}
	for (auto f:this->static_functions){d->static_functions.push_back((ExprFnDef*)f->clone());}
	for (auto f:this->static_fields){d->static_fields.push_back((ArgDef*)f->clone());}
	for (auto f:this->static_virtual){d->static_virtual.push_back((ArgDef*)f->clone());}
	for (auto s:this->structs){d->structs.push_back((ExprStructDef*)s->clone());}
	for (auto l:this->literals){d->literals.push_back((ExprLiteral*)l->clone());}
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
void ExprStructDef::roll_vtable() {
	
	if (this->is_vtable_built()){// ToDO: && is-class. and differentiate virtual functions. For the
		return;
	}
	dbg_vtable("rolling vtable for %s,inherits %s\n",str(this->name),this->inherits?str(this->inherits->name):"void");
	if (this->vtable){ return;} // done already
	if (this->inherits) {this->inherits->roll_vtable();}

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
			return;
		this->vtable=new ExprStructDef(this->pos,getStringIndexConcat(name,"__vtable_format"));
		this->vtable->vtable_name=getStringIndex("void__vtable");

		for (auto f:this->virtual_functions) {
			// todo: static-virtual fields go here!
			this->vtable->fields.push_back(
					new ArgDef(
					this->pos,
					f->name,
					f->fn_type,
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
		this->fields.insert(
			this->fields.begin(),
			new ArgDef(pos,__VTABLE_PTR,new Type(PTR,this->vtable)));
	}

	// TODO - more metadata to come here. struct layout; pointers,message-map,'isa'??
}
void ExprStructDef::dump(int depth) const{
	newline(depth);
	dbprintf("%s %s",this->kind_str(), getString(this->name));dump_typeparams(this->typeparams);
	dbprintf("[");
	if (this->instanced_types.size()){
		for (auto t:this->instanced_types)
		{	t->dump(depth+1);dbprintf(",");};
	}else{
		for (auto t:this->typeparams)
			{t->dump(depth+1);dbprintf(",");}
	}
	dbprintf("]");
	if (this->inherits) {dbprintf(" : %s", str(inherits->name));}
	dbprintf("{");
	for (auto m:this->literals)	{m->dump(depth+1);}
	for (auto m:this->fields)	{m->dump(depth+1);}
	for (auto s:this->structs)	{s->dump(depth+1);}
	for (auto f:this->functions){f->dump(depth+1);}
	for (auto f:this->virtual_functions){f->dump(depth+1);}
	newline(depth);dbprintf("}");
}

ResolvedType ExprStructDef::resolve(Scope* definer_scope,const Type* desired,int flags){

	definer_scope->add_struct(this);
	if (!this->get_type()) {
		this->set_type(new Type(this,this->name));	// name selects this struct
	}

	if (!this->is_generic()){
		auto sc=definer_scope->make_inner_scope(&this->scope,this,this);
		for (auto m:fields)			{m->resolve(sc,nullptr,flags);}
		for (auto m:static_fields)	{m->resolve(sc,nullptr,flags);}
		for (auto m:static_virtual)	{m->resolve(sc,nullptr,flags);}
		for (auto s:structs){
			s->resolve(sc,nullptr,flags);
		}
		for (auto f:functions){
			f->resolve(sc,nullptr,flags);
		}
		for (auto f:virtual_functions){
			f->resolve(sc,nullptr,flags);
		}

		if (this->inherits_type && !this->inherits){
			this->inherits_type->resolve(definer_scope,desired,flags);
			this->inherits=definer_scope->find_struct_named(this->inherits_type->name);
		}
		roll_vtable();
		
		/// TODO clarify that we dont resolve a vtable.
		//if (this->vtable) this->vtable->resolve(definer_scope,desired,flags);
	} else{
		for (auto ins=this->instances; ins; ins=ins->next_instance)
			ins->resolve(definer_scope,nullptr, flags);
	}

	return propogate_type_fwd(flags,this, desired);
}


