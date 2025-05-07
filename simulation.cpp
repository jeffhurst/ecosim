#include "entt/entt.hpp"
#include <vector>
#include <random>
#include <fstream>
#include <iostream>
#include <cmath>
#include <string>
#include <algorithm>

constexpr float PI = 3.14159265358979323846f;
constexpr int WIDTH  = 200;
constexpr int HEIGHT = 200;
constexpr int MAX_TICKS     = 5000;
constexpr int DAY_LENGTH    = 100;
constexpr int SEASON_LENGTH = 4 * DAY_LENGTH;
constexpr int SAVE_INTERVAL = 5;
constexpr float INITIAL_GRASS_PROB = 0.02f;
constexpr float MUTATION_STDDEV    = 0.05f;
constexpr int   RAIN_INTERVAL      = 150;
constexpr float RAIN_AMOUNT        = 1.0f;
constexpr float REPRODUCE_ENERGY   = 0.55f;
constexpr float   MATURITY_AGE_SCALE = 0.3f;

// occupancy grid
typedef unsigned long long ull;
static std::vector<bool> occupied( WIDTH*HEIGHT, false);
inline bool isOccupied(int x, int y)   { return occupied[y * WIDTH + x]; }
inline void setOccupied(int x, int y)  { occupied[y * WIDTH + x] = true; }
inline void clearOccupied(int x, int y){ occupied[y * WIDTH + x] = false; }

// Tile grid for abiotic components
enum class TileType { Soil, Water };
struct Tile { TileType type = TileType::Soil; float water = 10.0f; float nutrient = 5000.0f; };
static std::vector<Tile> grid;
inline Tile& at(int x, int y) { return grid[y * WIDTH + x]; }

// Components
struct Position { int x, y; };
struct Genes    { float sunlightEff, waterEff, nutrientEff, decayRate; };
struct Age      { int age = 0, maxAge = 100; };
struct Energy   { float value = 0.0f; };
struct Dead     { bool dead = false; };

// Counters
static ull energyDeaths = 0, waterDeaths = 0, oldAgeDeaths = 0, grassAlive = 0;
static float avgGrassEnergy = 0.0f;

// Random
std::mt19937_64 rng{12345};
std::normal_distribution<float> gauss(0.0f, MUTATION_STDDEV);
std::uniform_real_distribution<>  uni(0.0f,1.0f);

// Pool for dead entities
static std::vector<entt::entity> entityPool;

void generateWorld(unsigned seed=12345) {
    std::mt19937_64 Wrng(seed);
    grid.assign(WIDTH*HEIGHT, {});
    int cx = WIDTH/2, cy = HEIGHT/2, r = std::min(WIDTH,HEIGHT)/6;
    for(int y=0;y<HEIGHT;y++) for(int x=0;x<WIDTH;x++) {
        int dx=x-cx, dy=y-cy;
        if(dx*dx+dy*dy <= r*r) {
            auto &t = at(x,y);
            t.type = TileType::Water;
            t.water = 10.0f;
            t.nutrient = 0.0f;
        }
    }
    std::uniform_int_distribution<> edgeAng(0,359);
    for(int i=0;i<12;i++){
        float angle = edgeAng(Wrng)*PI/180.0f;
        float x = cx + r * std::cos(angle), y = cy + r * std::sin(angle);
        for(int step=0; step<WIDTH; step++){
            int xi = std::clamp(int(x),0,WIDTH-1);
            int yi = std::clamp(int(y),0,HEIGHT-1);
            at(xi,yi).type = TileType::Water;
            at(xi,yi).water = 8.0f;
            angle += (uni(Wrng)-0.5f)*0.4f;
            x += std::cos(angle);
            y += std::sin(angle);
        }
    }
}

void seedGrass(entt::registry &reg) {
    for(int y=0;y<HEIGHT;y++) for(int x=0;x<WIDTH;x++){
        if(at(x,y).type==TileType::Soil && !isOccupied(x,y) && uni(rng) < INITIAL_GRASS_PROB) {
            auto e = reg.create();
            reg.emplace<Position>(e, x, y);
            Genes g{1.0f+gauss(rng), 1.0f+gauss(rng), 1.0f+gauss(rng), 0.5f+gauss(rng)*0.1f};
            reg.emplace<Genes>(e, g);
            int agePlus = int(gauss(rng)*10.0+0.5);
            reg.emplace<Age>(e, Age{0, 50 + agePlus});
            reg.emplace<Energy>(e, Energy{0.5f});

            setOccupied(x,y);
            grassAlive++;
        }
    }
}

struct Serializer {
    std::vector<std::tuple<
        int, /* tick */
        int, /* id */
        int, /* x */
        int, /* y */
        int, /* age */
        int, /* maxAge */
        float, /* energy */
        float, /* sunEff */
        float, /* watEff */
        float, /* nutEff */
        float  /* decayRate */
    >> vegCache;

    std::vector<std::tuple<
        int,    /* tick */
        int,    /* totalEntities */
        unsigned long long, /* energyDeaths */
        unsigned long long, /* waterDeaths */
        unsigned long long, /* oldAgeDeaths */
        float   /* avgGrassEnergy */
    >> statsCache;
    std::ofstream veg_out, world_out, stats_out;

    Serializer() {
        statsCache.reserve(SAVE_INTERVAL);

        veg_out.open("grass_states.csv");
        stats_out.open("simulation_stats.csv");
        veg_out << "# WIDTH=" << WIDTH       << "\n"
                << "# HEIGHT=" << HEIGHT     << "\n"
                << "# MAX_TICKS=" << MAX_TICKS << "\n"
                << "# SAVE_INTERVAL=" << SAVE_INTERVAL << "\n";
        veg_out << "tick,id,x,y,age,maxAge,energy,sunEff,watEff,nutEff,decay\n";
        
        stats_out << "tick,totalEntities,energyDeaths,waterDeaths,oldAgeDeaths,avgGrassEnergy\n";

        world_out.open("world_state.csv");
        world_out << "x,y,type\n";
        for(int y=0;y<HEIGHT;y++) for(int x=0;x<WIDTH;x++){
            Tile &t = at(x,y);
            world_out << x << ',' << y << ','
                      << (t.type==TileType::Soil ? "Soil" : "Water") << '\n';
        }
    }

    void flushVegCache() {
        // 1) write vegCache
        for(auto &v : vegCache) {
            auto [tk, id, x, y, age, maxAge,
                energy, sunEff, watEff, nutEff, decayRate] = v;

            veg_out
            << tk << ',' << id  << ',' << x  << ',' << y
            << ',' << age << ',' << maxAge << ',' << energy
            << ',' << sunEff << ',' << watEff << ',' << nutEff
            << ',' << decayRate << '\n';
        }
        vegCache.clear();
       
    }

    void flushStatsCache() {
         // 2) write statsCache
        for(auto &s : statsCache) {
            auto [tk, totalEnt, ed, wd, od, avg] = s;
            stats_out
            << tk        << ',' 
            << totalEnt  << ',' 
            << ed        << ','
            << wd        << ','
            << od        << ','
            << avg       << '\n';
            }
            statsCache.clear();
    }

    void saveTick(int tick, int totalEntities, entt::registry &reg) {
        reg.view<Position,Age,Energy,Genes>().each(
          [&](auto id, auto &pos, auto &age, auto &e, auto &g){
            vegCache.emplace_back(tick, int(id), pos.x, pos.y, age.age, age.maxAge, e.value, g.sunlightEff, g.waterEff, g.nutrientEff, g.decayRate);
        });
        statsCache.emplace_back(tick,totalEntities,energyDeaths,waterDeaths,oldAgeDeaths,avgGrassEnergy);
        energyDeaths = waterDeaths = oldAgeDeaths = avgGrassEnergy = 0;
    }

    void saveStatsCache() {
        flushVegCache();
        flushStatsCache();
    }
};

float sunlight(int tick) {
    int seasonalTick = tick % SEASON_LENGTH;
    float dayLen = DAY_LENGTH * (1.0f + 0.2f * std::sin(2*PI*seasonalTick/SEASON_LENGTH));
    float tmod   = tick % int(dayLen);
    return std::clamp(1.0f - std::abs((tmod/dayLen)*2 - 1), 0.0f, 1.0f);
}

int main(){
    entt::registry reg;
    generateWorld(42);
    seedGrass(reg);
    Serializer ser;

    // pre-allocated buffers
    std::vector<entt::entity> toKill;
    std::vector<std::tuple<Position, Genes, Age, Energy>> births;
    
    toKill.reserve(WIDTH * HEIGHT / 2);
    births.reserve(WIDTH * HEIGHT / 2);
    entityPool.reserve(WIDTH * HEIGHT);
    


    
    // Main loop
    for(int tick=0; tick<MAX_TICKS; tick++){
        toKill.clear();
        births.clear();

        // cached view
        auto viewAlive = reg.view<Position, Age, Energy, Genes>(entt::exclude<Dead>);

        // variables for loop
        float sunI = sunlight(tick);
        float sum = 0.0f; 
        int count = 0;

        // primary view loop of living grass. 
        viewAlive.each([&](auto entity, auto &pos, auto &age, auto &en, auto &g){
            // Energy Update
            en.value += sunI * g.sunlightEff * 0.1f;
            Tile &t = at(pos.x,pos.y);
            float takenW = std::min(t.water, g.waterEff * 0.05f);
            t.water  -= takenW; en.value += takenW;
            float takenN = std::min(t.nutrient, g.nutrientEff * 0.05f);
            t.nutrient -= takenN; en.value += takenN;
            
            // grow, age, kill
            count++; sum += en.value; age.age++;
            if(t.water <= 0.0f) {
                waterDeaths++; t.nutrient += std::max(en.value, 0.5f);
                toKill.push_back(entity);
            } else if(en.value <= 0.2f) {
                energyDeaths++; t.nutrient += std::max(en.value, 1.0f);
                toKill.push_back(entity);
            } else if(age.age >= age.maxAge) {
                oldAgeDeaths++; t.nutrient += std::max(en.value, 1.0f);
                toKill.push_back(entity);
            }
            

            // reproduction: reuse pooled entities if available
            if(grassAlive < WIDTH * HEIGHT){
                if(age.age >= MATURITY_AGE_SCALE * age.maxAge && en.value >= REPRODUCE_ENERGY){
                    int dx = int(uni(rng)*3)-1, dy = int(uni(rng)*3)-1;
                    int nx = pos.x + dx, ny = pos.y + dy;
                    if(nx>=0 && nx<WIDTH && ny>=0 && ny<HEIGHT
                       && at(nx,ny).type==TileType::Soil && !isOccupied(nx,ny)){
        
                        Genes ng = g; 
                        ng.sunlightEff += gauss(rng);
                        ng.waterEff    += gauss(rng);
                        ng.nutrientEff += gauss(rng);
                        ng.decayRate   += gauss(rng)*0.02f;
                        int parentMax = age.maxAge;
                        Position newPos{nx,ny};
                        Age newAge{0, std::max(10, int(parentMax + gauss(rng)*10+0.1))};
                        Energy newEnergy{0.5f};
                        births.emplace_back(newPos,ng,newAge,newEnergy);
                        en.value *= 0.1f;
                    }
                }
            }     
        });

        // mark dead and pool
        for(auto e : toKill){
            reg.emplace<Dead>(e);        // now marking it dead
            auto &pos = reg.get<Position>(e);
            clearOccupied(pos.x,pos.y);
            auto &d = reg.get<Dead>(e);
            d.dead = true;
            grassAlive--;
            entityPool.push_back(e);
        }

        // produce new grass
        for (auto b : births) {
            entt::entity e2;
            if(!entityPool.empty()){
                e2 = entityPool.back(); entityPool.pop_back();
                reg.remove<Dead>(e2);
                reg.replace<Position>(e2, std::get<0>(b));
                reg.replace<Genes>(e2, std::get<1>(b));
                reg.replace<Age>(e2, std::get<2>(b));
                reg.replace<Energy>(e2, std::get<3>(b));
            } else {
                e2 = reg.create();
                reg.emplace<Position>(e2, std::get<0>(b));
                reg.emplace<Genes>(e2, std::get<1>(b));
                reg.emplace<Age>(e2, std::get<2>(b));
                reg.emplace<Energy>(e2, std::get<3>(b));
            }
            setOccupied(std::get<0>(b).x,std::get<0>(b).y);
            grassAlive++;
        }

        // environment systems
        // rain system
        if(tick % RAIN_INTERVAL == 0){
            for(auto &t : grid) if(t.type==TileType::Soil) t.water += RAIN_AMOUNT;
        }

        // stats
        avgGrassEnergy = count ? sum/count : 0.0f;

        
        ser.saveTick(tick, grassAlive, reg);
        if(tick % SAVE_INTERVAL == 0) {
                ser.saveStatsCache();   
        }
        if(tick % SAVE_INTERVAL*10 == 0) std::cout << tick << "\n";

    }
    ser.saveStatsCache(); // final cache flush
    std::cout << "Simulation complete. Data -> grass_states.csv, world_state.csv, simulation_stats.csv\n";
    return 0;
}
