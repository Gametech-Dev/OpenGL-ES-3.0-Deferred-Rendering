/** @file exporter.cpp
 *  @copyright Copyright (c) 2013. All rights reserved.
 */
#include "../src/vec_math.h"
#include "../src/assert.h"
extern "C" {
#include "../src/utility.h"
}
#include <stdlib.h>
#include <stddef.h>
#include <vector>
#include <string>
#include <map>
#include <stdio.h>
#include <sstream>

/* Constants
 */

/* Types
 */

struct SimpleVertex
{
    Vec3    position;
    Vec3    normal;
    Vec2    texcoord;
};
struct Vertex
{
    Vec3    position;
    Vec3    normal;
    Vec3    tangent;
    Vec3    bitangent;
    Vec2    texcoord;
};
struct MeshData
{
    char        name[128];
    Vertex*     vertices;
    uint32_t    indices;
    uint32_t    vertex_count;
    uint32_t    index_count;
};
struct MaterialData
{
    char        name[128];
    char        albedo_tex[128];
    char        normal_tex[128];
    Vec3        specular_color;
    float       specular_power;
    float       specular_coefficient;
};
struct ModelData
{
    char    mesh_name[128];
    char    material_name[128];
};
struct SceneData
{
    MeshData*       meshes;
    MaterialData*   materials;
    ModelData*      models;
    uint32_t        num_meshes;
    uint32_t        num_materials;
    uint32_t        num_models;
};

/* Variables
 */

/* Internal functions
 */
static int load_file_data(const char* filename, void** data, size_t* data_size)
{
    FILE*   file = fopen(filename, "rb");
    assert(file);

    fseek(file, 0, SEEK_END);
    *data_size = (size_t)ftell(file);
    fseek(file, 0, SEEK_SET);

    *data = malloc(*data_size);
    assert(*data);

    fread(*data, *data_size, 1, file);
    assert(ferror(file) == 0);
    fclose(file);

    return 0;
}
static void free_file_data(void* data)
{
    free(data);
}
static void _load_mtl_file(const char* filename, SceneData* scene)
{
    char* file_data = NULL;
    char* original_data = NULL;
    size_t file_size = 0;
    int matches;

    load_file_data(filename, (void**)&file_data, &file_size);
    original_data = file_data;


    //
    // Count materials first
    //
    int orig_num_materials = scene->num_materials;
    while(1) {
        if(file_data == NULL)
            break;
        char line[1024] = {0};
        file_data = (char*)get_line_from_buffer(line, sizeof(line), file_data);
        char line_header[64] = {0};
        sscanf(line, "%s", line_header);
        
        if(strcmp(line_header, "newmtl") == 0) {
            scene->num_materials++;
        }
    }
    file_data = original_data;

    //
    // Allocate materials
    //
    scene->materials = (MaterialData*)realloc(scene->materials, scene->num_materials*sizeof(MaterialData));
    MaterialData* current_material = (scene->materials - 1) + orig_num_materials;

    //
    // Loop through and create materials
    //
    while(1) {
        if(file_data == NULL)
            break;
        char line[1024] = {0};
        file_data = (char*)get_line_from_buffer(line, sizeof(line), file_data);
        char line_header[64] = {0};
        sscanf(line, "%s", line_header);

        if(strcmp(line_header, "newmtl") == 0) {
            current_material++;
            matches = sscanf(line, "%s %s\n", line_header, current_material->name);
            current_material->specular_power = 16.0f;
            assert(matches == 2);
        } else if(strcmp(line_header, "map_Kd") == 0) {
            matches = sscanf(line, "%s %s\n", line_header, current_material->albedo_tex);
            assert(matches == 2);
        } else if((strcmp(line_header, "map_bump") == 0 || strcmp(line_header, "map_bump") == 0) && current_material->normal_tex[0] == '\0') {
            matches = sscanf(line, "%s %s\n", line_header, current_material->normal_tex);
            assert(matches == 2);
        } else if(strcmp(line_header, "Ks") == 0) {
            Vec3 spec_color;
            matches = sscanf(line, "%s %f %f %f\n", line_header, &spec_color.x, &spec_color.y, &spec_color.z);
            assert(matches == 4);
            current_material->specular_color = spec_color;
        } else if(strcmp(line_header, "Ns") == 0) {
            matches = sscanf(line, "%s %f\n", line_header, &current_material->specular_coefficient);
            assert(matches == 2);
        }
    }
    free_file_data(original_data);
}

static Vertex* calculate_tangets(const SimpleVertex* vertices, int num_vertices, const void* indices, size_t index_size, int num_indices)
{
    Vertex* new_vertices = new Vertex[num_vertices];
    for(int ii=0;ii<num_vertices;++ii) {
        new_vertices[ii].position = vertices[ii].position;
        new_vertices[ii].normal = vertices[ii].normal;
        new_vertices[ii].texcoord = vertices[ii].texcoord;
    }
    for(int ii=0;ii<num_indices;ii+=3) {
        uint32_t i0,i1,i2;
        if(index_size == 2) {
            i0 = ((uint16_t*)indices)[ii+0];
            i1 = ((uint16_t*)indices)[ii+1];
            i2 = ((uint16_t*)indices)[ii+2];
        } else {
            i0 = ((uint32_t*)indices)[ii+0];
            i1 = ((uint32_t*)indices)[ii+1];
            i2 = ((uint32_t*)indices)[ii+2];
        }

        Vertex& v0 = new_vertices[i0];
        Vertex& v1 = new_vertices[i1];
        Vertex& v2 = new_vertices[i2];

        Vec3 delta_pos1 = vec3_sub(v1.position, v0.position);
        Vec3 delta_pos2 = vec3_sub(v2.position, v0.position);
        Vec2 delta_uv1 = vec2_sub(v1.texcoord, v0.texcoord);
        Vec2 delta_uv2 = vec2_sub(v2.texcoord, v0.texcoord);

        float r = 1.0f / (delta_uv1.x * delta_uv2.y - delta_uv1.y * delta_uv2.x);
        Vec3 a = vec3_mul_scalar(delta_pos1, delta_uv2.y);
        Vec3 b = vec3_mul_scalar(delta_pos2, delta_uv1.y);
        Vec3 tangent = vec3_sub(a,b);
        tangent = vec3_mul_scalar(tangent, r);

        a = vec3_mul_scalar(delta_pos2, delta_uv1.x);
        b = vec3_mul_scalar(delta_pos1, delta_uv2.x);
        Vec3 bitangent = vec3_sub(a,b);
        bitangent = vec3_mul_scalar(bitangent, r);

        v0.bitangent = bitangent;
        v1.bitangent = bitangent;
        v2.bitangent = bitangent;

        v0.tangent = tangent;
        v1.tangent = tangent;
        v2.tangent = tangent;
    }
    return new_vertices;
}
struct int3 {
    int p;
    int t;
    int n;

    bool operator==(const int3 rh) const
    {
        return p == rh.p && t == rh.t && n == rh.n;
    }
    bool operator<(const int3 rh) const
    {
        if(p < rh.p) return true;
        if(p > rh.p) return false;

        if(t < rh.t) return true;
        if(t > rh.t) return false;

        if(n < rh.n) return true;
        if(n > rh.n) return false;

        return false;
    }
};
struct Triangle
{
    int3    vertex[3];
};
//FILE* mesh_file = fopen((mesh_name + ".mesh").c_str(), "wb");
//
//// mtl_name_len
//uint32_t material_name_len = (uint32_t)strlen(mesh_material_pairs[(uint32_t)jj].c_str());
//fwrite(&material_name_len, 1, sizeof(material_name_len), mesh_file);
//// mtl_name
//fwrite(mesh_material_pairs[(uint32_t)jj].c_str(), 1, material_name_len, mesh_file);
//// vertex count
//fwrite(&vertex_count, 1, sizeof(vertex_count), mesh_file);
//// index count
//fwrite(&index_count, 1, sizeof(index_count), mesh_file);
//// vertices
//fwrite(new_vertices, sizeof(Vertex), vertex_count, mesh_file);
//// indices
//fwrite(&i[0], sizeof(uint32_t), index_count, mesh_file);
//
//fclose(mesh_file);

static void _load_obj(const char* filename, SceneData* scene)
{
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> texcoords;

    std::vector<std::vector<Triangle> >  all_triangles;
    std::vector<std::string>  mesh_material_pairs;
    std::vector<std::string>  mesh_names;
    int current_indices = -1;

    //std::map<std::string, Material> all_materials;

    int textured = 0;

    Vec2 tex = {0.5f, 0.5f};
    texcoords.push_back(tex);

    char* file_data = NULL;
    char* original_data = NULL;
    size_t file_size = 0;
    int matches;

    load_file_data(filename, (void**)&file_data, &file_size);
    original_data = file_data;

    int orig_num_meshes = scene->num_meshes;
    int orig_num_models = scene->num_models;

    //
    // Count everything
    //
    uint32_t num_total_vertices = 0;
    uint32_t num_total_texcoords = 0;
    uint32_t num_total_normals = 0;
    uint32_t num_meshes = 0;
    while(1) {
        if(file_data == NULL)
            break;
        char line[1024] = {0};
        file_data = (char*)get_line_from_buffer(line, sizeof(line), file_data);
        char line_header[16] = {0};
        sscanf(line, "%s", line_header);

        if(strcmp(line_header, "v") == 0) {
            num_total_vertices++;
        } else if(strcmp(line_header, "vt") == 0) {
            num_total_texcoords++;
        } else if(strcmp(line_header, "vn") == 0) {
            num_total_normals++;
        } else if(strcmp(line_header, "usemtl") == 0) {
            num_meshes++;
            scene->num_meshes++;
            scene->num_models++;
        } else if(strcmp(line_header, "mtllib") == 0 ) {
            char mtl_filename[256];
            matches = sscanf(line, "%s %s\n", line_header, mtl_filename);
            assert(matches == 2);
            _load_mtl_file(mtl_filename, scene);
        }
    }
    original_data = file_data;
    positions.reserve(num_total_vertices);
    normals.reserve(num_total_normals);
    texcoords.reserve(num_total_texcoords);
    all_triangles.resize(num_meshes);

    scene->meshes = (MeshData*)realloc(scene->meshes, sizeof(ModelData)*scene->num_meshes);
    scene->models = (ModelData*)realloc(scene->models, sizeof(ModelData)*scene->num_models);

    //
    // Fill out vertex data
    //
    while(1) {
        if(file_data == NULL)
            break;
        char line[1024] = {0};
        file_data = (char*)get_line_from_buffer(line, sizeof(line), file_data);
        char line_header[16] = {0};
        sscanf(line, "%s", line_header);

        if(strcmp(line_header, "v") == 0) {
            Vec3 v;
            matches = sscanf(line, "%s %f %f %f\n", line_header, &v.x, &v.y, &v.z);
            assert(matches == 4);
            positions.push_back(v);
            textured = 0;
        } else if(strcmp(line_header, "vt") == 0) {
            Vec2 t;
            matches = sscanf(line, "%s %f %f\n", line_header, &t.x, &t.y);
            assert(matches == 3);
            texcoords.push_back(t);
            textured = 1;
        } else if(strcmp(line_header, "vn") == 0) {
            Vec3 n;
            matches = sscanf(line, "%s %f %f %f\n", line_header, &n.x, &n.y, &n.z);
            assert(matches == 4);
            normals.push_back(n);
        }
    }
    original_data = file_data;

    //
    // Load mesh data
    //
    std::vector<Triangle>* mesh_triangles = &all_triangles[0] - 1;
    MeshData* current_mesh = (scene->meshes - 1) + orig_num_meshes;
    ModelData* current_model = (scene->models - 1) + orig_num_models;

    const char* prev_line = NULL;
    while(1) {
        if(file_data == NULL)
            break;
        char line[1024] = {0};
        const char* this_line = file_data;
        file_data = (char*)get_line_from_buffer(line, sizeof(line), file_data);
        const char* next_line = file_data;

        char line_header[16] = {0};
        sscanf(line, "%s", line_header);

        if(strcmp(line_header, "usemtl") == 0) {
            ++mesh_triangles;
            ++current_mesh;
            ++current_model;
            // Get material name
            char material_name[256];
            matches = sscanf(line, "%s %s\n", line_header, material_name);
            assert(matches == 2);
            // Check to see if this is named (a 'g' on the next or prev line)
            if(prev_line && prev_line[0] == 'g') {
                matches = sscanf(prev_line, "%s %s\n", line_header, current_mesh->name);
                assert(matches == 2);
            } else if(next_line && next_line[0] == 'g') {
                matches = sscanf(next_line, "%s %s\n", line_header, current_mesh->name);
                assert(matches == 2);
            } else {
                std::ostringstream s;
                s << "mesh";
                s << current_mesh - scene->meshes;
                strncpy(current_mesh->name, s.str().c_str(), sizeof(current_mesh->name));
            }
            strncpy(current_model->mesh_name, current_mesh->name, sizeof(current_mesh->name));
            strncpy(current_model->material_name, material_name, sizeof(material_name));

        } else if(strcmp(line_header, "f") == 0) {
            int3 triangle[4];
            if(textured) {
                matches = sscanf(line, "%s %d/%d/%d %d/%d/%d %d/%d/%d %d/%d/%d\n",
                                 line_header,
                                 &triangle[0].p, &triangle[0].t, &triangle[0].n,
                                 &triangle[1].p, &triangle[1].t, &triangle[1].n,
                                 &triangle[2].p, &triangle[2].t, &triangle[2].n,
                                 &triangle[3].p, &triangle[3].t, &triangle[3].n);
                if(matches != 10 && matches != 13) {
                    printf("Can't load this OBJ\n");
                    free_file_data(original_data);
                    exit(1);
                }
            } else {
                matches = sscanf(line, "%s %d//%d %d//%d %d//%d %d//%d\n",
                                 line_header,
                                 &triangle[0].p, &triangle[0].n,
                                 &triangle[1].p, &triangle[1].n,
                                 &triangle[2].p, &triangle[2].n,
                                 &triangle[3].p, &triangle[3].n);
                if(matches != 7 && matches != 9) {
                    printf("Can't load this OBJ\n");
                    free_file_data(original_data);
                    exit(1);
                }
                triangle[0].t = 0;
                triangle[1].t = 0;
                triangle[2].t = 0;
                if(matches == 9) {
                    triangle[3].t = 0;
                    matches = 13;
                }
            }
            Triangle tri = {
                triangle[0],
                triangle[1],
                triangle[2],
            };
            mesh_triangles->push_back(tri);
            if(matches == 13) {
                Triangle tri2 = {
                    triangle[0],
                    triangle[2],
                    triangle[3],
                };
                mesh_triangles->push_back(tri2);
            }
        }
        prev_line = this_line;
    }

    //
    // Create meshes
    //
    for(int jj=0; jj<(int)all_indices.size();++jj) {
        std::vector<int3>& indices = all_indices[(uint32_t)jj];

        std::map<int3, int> m;
        std::vector<SimpleVertex> v;
        std::vector<uint32_t> i;
        int num_indices = (int)indices.size();
        for(int ii=0;ii<num_indices;++ii) {
            int3 index = indices[(uint32_t)ii];
            std::map<int3, int>::iterator iter = m.find(index);
            if(iter != m.end()) {
                /* Already exists */
                i.push_back((uint32_t)iter->second);
            } else {
                /* Add it */
                int pos_index = indices[(uint32_t)ii].p-1;
                int tex_index = indices[(uint32_t)ii].t;
                int norm_index = indices[(uint32_t)ii].n-1;
                SimpleVertex vertex;
                vertex.position = positions[(uint32_t)pos_index];
                vertex.texcoord = texcoords[(uint32_t)tex_index];
                vertex.normal = normals[(uint32_t)norm_index];
                /* Flip v-channel */
                vertex.texcoord.y = 1.0f-vertex.texcoord.y;

                i.push_back((uint32_t)v.size());
                m[index] = (int)v.size();
                v.push_back(vertex);
            }
        }
        uint32_t vertex_count = (uint32_t)v.size();
        uint32_t index_count = (uint32_t)i.size();
        Vertex* new_vertices = calculate_tangets(&v[0], (int)vertex_count, &i[0], sizeof(uint32_t), (int)index_count);

        //Mesh* mesh = create_mesh(new_vertices, sizeof(Vertex)*vertex_count, &i[0], sizeof(uint32_t)*index_count, index_count);

        std::string mesh_name;
        if(jj >= (int)mesh_names.size()) {
            static int mesh_number = 0;
            std::ostringstream s;
            mesh_name = "mesh";
            s << mesh_number++;
            mesh_name += s.str();
        } else {
            mesh_name = mesh_names[(uint32_t)jj];
        }

        delete [] new_vertices;

        //all_meshes.push_back(mesh);
    }
    // *meshes = (Mesh**)calloc(all_meshes.size(),sizeof(Mesh*));
    // for(int ii=0;ii<(int)all_meshes.size(); ++ii) {
    //     (*meshes)[ii] = all_meshes[ii];
    // }
    // *num_meshes = (int)all_meshes.size();

//    if(materials) {
//        *materials = (Material*)calloc(all_materials.size(),sizeof(Material));
//        for(int ii=0;ii<(int)mesh_material_pairs.size();++ii) {
//            (*materials)[ii] = all_materials[mesh_material_pairs[ii]];
//        }
//        *num_materials = (int)all_materials.size();
//    }
    free_file_data(original_data);
}

/* External functions
 */
int main(int argc, const char *argv[])
{
    SceneData scene = {0};
    for(int ii=1; ii<argc;++ii) {
        _load_obj(argv[ii], &scene);
    }
    return 0;
}