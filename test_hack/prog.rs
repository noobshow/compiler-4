fn lerp(a,b,f)->float{(b-a)*f+a};
fn foo1(a:*char)->void;
fn printf(s:str,...)->int;

struct Foo {
	vx:int, vy:int, vz:int
}
struct Vec4{ vx:int,vy:int,vz:int,vw:int}
struct Triangle{ i0:int,i1:int,i2:int}
struct Mesh {
	vertices:array[Vec4,20],
	triangles:array[Triangle,5]
}
fn something(f:Foo)->int{
	printf("f.x %d,.y %d,.z %d\n", f.vx, f.vy, f.vz);
	0
}

fn foo()->int{
	printf("hello from foo\n");
	0
}
fn bar()->int{
	printf("hello from bar\n");
	0
}

fn main(argc:int,argv:**char)->int{
	xs=:array[int,512];
	q:=xs[1]; p1:=&xs[1];
	xs[2]=000;
	xs[2]+=400;
	*p1=30;
	z:=5;
	y:=xs[1]+z+xs[2];
	x:=0;
	fv=:Foo;
	fv.vx=3; fv.vy=4; fv.vz=5;
	something(fv);


	mv:=Vec4{vx=0,vy=1,vz=20};
	mv2:={key=12,value=13};

	ff:=mv.vz;
	
	for i:=0,j:=0; i<10; i+=1,j+=10 {
		x+=i;
		printf("i,j=%d,%d,x=%d\n",i,j,x);
	}else{
		printf("loop exit fine\n");
	}

	x:=if argc<2{printf("argc is <2\n");1}else{printf("argc is>2\n");2};
	printf("yada yada yada\n");
	printf("\nHelloWorld %.3f %d %d foo bar baz\n",lerp(5.0,10.0,7.0), mv.vz , mv2.key);
	0
}







