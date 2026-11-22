/* A seeded halo/filament point-process catalogue. It represents a statistical
   cosmic web, not a measured galaxy catalogue or a gravitational N-body solve.
   The small graph is built once per seed and shared by every particle chunk. */
#define UC_NODES 384u
#define UC_EDGES 1152u
static struct {
    up_uint nodes, edges;
    unsigned seed, valid, edge_count;
} up_cosmic;
typedef struct { float x,y,z,mass,cdf,radius,core,color; } uc_node;
typedef struct { up_uint a,b;float weight,pad; } uc_edge;
static unsigned uc_hash(unsigned x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
static float uc_random(unsigned x){return ((float)(uc_hash(x)&0xffffffu)+.5f)/16777216.f;}
static float uc_normal(unsigned x){return sqrtf(-2.f*logf(uc_random(x)))*cosf(6.28318530718f*uc_random(x+97u));}
static void up_cosmic_destroy(void){
    if(up_cosmic.nodes)up_glDeleteBuffers(1,&up_cosmic.nodes);
    if(up_cosmic.edges)up_glDeleteBuffers(1,&up_cosmic.edges);
    memset(&up_cosmic,0,sizeof up_cosmic);
}
static void uc_anchor(uc_node *n,float x,float y,float z,float mass){*n=(uc_node){x,y,z,mass,0.f,0.f,0.f,0.f};}
static float uc_distance2(uc_node a,uc_node b){float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return x*x+y*y+z*z;}
static int up_cosmic_prepare(unsigned seed){
    if(up_cosmic.valid&&up_cosmic.seed==seed){
        up_glBindBufferBase(0x90D2,4,up_cosmic.nodes);up_glBindBufferBase(0x90D2,5,up_cosmic.edges);return 1;
    }
    uc_node nodes[UC_NODES],roots[24];uc_edge edges[UC_EDGES];unsigned count=0;
    for(unsigned j=0;j<24;j++){
        unsigned k=seed+j*17477u;float x=0,y=0,z=0;
        for(unsigned trial=0;trial<64;trial++){
            x=(uc_random(k+trial*3)-.5f)*480.f;y=(uc_random(k+trial*3+1)-.5f)*480.f;z=(uc_random(k+trial*3+2)-.5f)*350.f;
            int ok=1;for(unsigned prev=0;prev<j;prev++)if(uc_distance2((uc_node){x,y,z,1,0,0,0,0},roots[prev])<65.f*65.f)ok=0;
            if(ok)break;
        }
        uc_anchor(&roots[j],x,y,z,1.f);
    }
    // Two labelled anchors are schematic regions; their masses are model parameters.
    uc_anchor(&roots[0],40.f,-52.f,10.f,1.f);uc_anchor(&roots[1],130.f,-90.f,70.f,1.f);
    for(unsigned j=0;j<UC_NODES;j++){
        unsigned k=seed+j*32971u,group=j/16u;uc_node center=roots[group];
        float spread=group<2?20.f:15.f+20.f*uc_random(seed+group*33u);
        float x=center.x+spread*uc_normal(k),y=center.y+spread*.8f*uc_normal(k+1),z=center.z+spread*.55f*uc_normal(k+2);
        float dx=x-20.f,dy=y-12.f,dz=z-8.f,r=sqrtf(dx*dx+dy*dy+dz*dz);
        // An underdense local basin has a populated rim, never a uniform luminous sheet.
        if(r<25.f){float f=(25.f+5.f*uc_random(k+7))/fmaxf(r,.01f);x=20.f+dx*f;y=12.f+dy*f;z=8.f+dz*f;}
        float mass=expf(uc_normal(k+3)*.85f-.2f);
        uc_anchor(&nodes[j],x,y,z,mass);
    }
    uc_anchor(&nodes[0],40.f,-52.f,10.f,7.f);uc_anchor(&nodes[16],130.f,-90.f,70.f,9.f);
    float total_mass=0.f;
    for(unsigned j=0;j<UC_NODES;j++)total_mass+=powf(nodes[j].mass,.8f);
    float cdf=0.f;
    for(unsigned j=0;j<UC_NODES;j++){
        cdf+=powf(nodes[j].mass,.8f)/total_mass;nodes[j].cdf=cdf;
        nodes[j].radius=1.2f+2.0f*cbrtf(nodes[j].mass);
        nodes[j].core=.35f+.4f*cbrtf(nodes[j].mass);
        nodes[j].color=uc_random(seed+j*17u);
    }
    nodes[UC_NODES-1].cdf=1.f;
    unsigned char connected[UC_NODES][UC_NODES]={{0}};
    // A minimum spanning tree supplies connected filaments without tessellation walls.
    float best[UC_NODES];unsigned parent[UC_NODES];unsigned char used[UC_NODES]={0};
    for(unsigned i=0;i<UC_NODES;i++){best[i]=1e30f;parent[i]=0;}best[0]=0;
    for(unsigned step=0;step<UC_NODES;step++){
        unsigned u=0;float min=1e30f;for(unsigned i=0;i<UC_NODES;i++)if(!used[i]&&best[i]<min){u=i;min=best[i];}
        used[u]=1;if(step&&count<UC_EDGES){edges[count++]=(uc_edge){parent[u],u,1.f,0.f};connected[parent[u]][u]=connected[u][parent[u]]=1;}
        for(unsigned v=0;v<UC_NODES;v++)if(!used[v]){float d=uc_distance2(nodes[u],nodes[v]);if(d<best[v]){best[v]=d;parent[v]=u;}}
    }
    // Short additional neighbours create irregular branch points and unequal loops.
    for(unsigned a=0;a<UC_NODES&&count+2<UC_EDGES;a++)for(unsigned rank=0;rank<1;rank++){
        unsigned b=UC_NODES;float nearest=1e30f;
        for(unsigned v=a+1;v<UC_NODES;v++){
            if(connected[a][v])continue;
            int triangle=0;for(unsigned w=0;w<UC_NODES;w++)if(connected[a][w]&&connected[v][w]){triangle=1;break;}
            if(triangle)continue;
            float d=uc_distance2(nodes[a],nodes[v]);if(d<nearest){nearest=d;b=v;}
        }
        if(b<UC_NODES&&nearest<65.f*65.f){edges[count++]=(uc_edge){a,b,.5f,0.f};connected[a][b]=connected[b][a]=1;}
    }
    if(!up_cosmic.nodes)up_glGenBuffers(1,&up_cosmic.nodes);
    if(!up_cosmic.edges)up_glGenBuffers(1,&up_cosmic.edges);
    up_glBindBuffer(0x90D2,up_cosmic.nodes);up_glBufferData(0x90D2,sizeof nodes,nodes,0x88E4);
    up_glBindBuffer(0x90D2,up_cosmic.edges);up_glBufferData(0x90D2,(up_size)count*sizeof(uc_edge),edges,0x88E4);
    if(dgl_glGetError()){up_log("cosmic graph","GPU catalogue allocation failed");return 0;}
    up_cosmic.seed=seed;up_cosmic.valid=1;up_cosmic.edge_count=count;
    up_glBindBufferBase(0x90D2,4,up_cosmic.nodes);up_glBindBufferBase(0x90D2,5,up_cosmic.edges);
    return 1;
}
