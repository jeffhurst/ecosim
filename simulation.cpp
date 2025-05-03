// simulation.cpp
// Build with: g++ -std=c++17 simulation.cpp -o sim

#include "entt/entt.hpp"
#include <vector>
#include <random>
#include <fstream>
#include <iostream>
#include <cmath>
#include <string>
#include <algorithm>

constexpr float PI = 3.14159265358979323846f; // π constant

// world dimensions
constexpr int WIDTH  = 200;
constexpr int HEIGHT = 200;

// simulation parameters
constexpr int MAX_TICKS        = 10000;
constexpr int DAY_LENGTH       = 100;    // ticks per day at equinox
constexpr int SEASON_LENGTH    = 4 * DAY_LENGTH; // 4 seasons cycle
constexpr int SAVE_INTERVAL    = 100;     // ticks between serialization
constexpr float INITIAL_GRASS_PROB = 0.02f; // 2% chance per tile

// mutation strength
constexpr float MUTATION_STDDEV = 0.05f;

// rain parameters
constexpr int   RAIN_INTERVAL = 500; // every 500 ticks
constexpr float RAIN_AMOUNT   = 1.0f; // water units per soil tile

// nutrient uptake thresholds
constexpr float REPRODUCE_ENERGY = 3.0f;
constexpr int   MATURITY_AGE     = 20;

// --- Tile grid for abiotic components ---
enum class TileType { Soil, Water };
struct Tile {
    TileType type = TileType::Soil; // default to soil
    float    water = 5.0f;   // initial water amout
    float    nutrient = 5.0f; // initial soil nutrient
};
static std::vector<Tile> grid;
inline Tile& at(int x, int y) { return grid[y * WIDTH + x]; }

// --- Components for grass entities ---
struct Position { int x, y; };
struct Genes {
    float sunlightEff, waterEff, nutrientEff, decayRate;
};
struct Age {
    int age = 0;
    int maxAge = 100;
};
struct Energy { float value = 0.0f; };

// counters
typedef unsigned long long ull;
static ull energyDeaths = 0, waterDeaths = 0, oldAgeDeaths = 0, grassAlive = 0;

// random utilities
std::mt19937_64 rng{12345};
std::normal_distribution<float>   gauss(0.0f, MUTATION_STDDEV);
std::uniform_real_distribution<>  uni(0.0f,1.0f);

// —— Helper: generate circular lake + branching rivers ——
void generateWorld(unsigned seed=12345) {
    std::mt19937_64 Wrng(seed);
    grid.assign(WIDTH*HEIGHT, {});
    // lake: circle at center
    int cx = WIDTH/2, cy = HEIGHT/2, r = std::min(WIDTH,HEIGHT)/6;
    for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
        int dx=x-cx, dy=y-cy;
        if(dx*dx+dy*dy <= r*r) {  // if inside circle
            auto &t=at(x,y);      // reference to tile
            t.type=TileType::Water;           
            t.water=10.0f; t.nutrient=0.0f;
        }
    }
    // rivers: random walks from lake edge outward
    std::uniform_int_distribution<> edgeAng(0,359);
    for(int i=0;i<12;i++){                             // num rivers
        float angle = edgeAng(Wrng)*PI/180.0f;         // random start angle
        float x = cx + r * std::cos(angle), y = cy + r * std::sin(angle);
        for(int step=0; step<WIDTH; step++){
            int xi = std::clamp(int(x),0,WIDTH-1);
            int yi = std::clamp(int(y),0,HEIGHT-1);
            at(xi,yi).type=TileType::Water;
            at(xi,yi).water=8.0f;
            // random turn
            angle += (uni(Wrng)-0.5f)*0.4f;
            x += std::cos(angle);
            y += std::sin(angle);
        }
    }
}
// —— Plant initial grass randomly across soil tiles ——
void seedGrass(entt::registry &reg) {
    for(int y=0;y<HEIGHT;y++)for(int x=0;x<WIDTH;x++){
        if(at(x,y).type==TileType::Soil && uni(rng) < INITIAL_GRASS_PROB) {
            auto e = reg.create();                   // create grass entity
            reg.emplace<Position>(e, x,y);           // add position component
            Genes g;
            g.sunlightEff = 1.0f + gauss(rng);
            g.waterEff    = 1.0f + gauss(rng);
            g.nutrientEff = 1.0f + gauss(rng);
            g.decayRate   = 0.5f + gauss(rng)*0.1f;
            reg.emplace<Genes>(e, g);
            reg.emplace<Age>(e, Age{0, 50 + int(gauss(rng)*10)});
            reg.emplace<Energy>(e, Energy{5.0f});
            grassAlive++; 
        }
    }
}

// —— Serializer: writes vegetation, world state, and stats to CSVs ——
struct Serializer {
    std::ofstream veg_out, world_out, stats_out;
    Serializer(const std::string &veg_fn="grass_states.csv",
               const std::string &world_fn="world_state.csv",
               const std::string &stats_fn="simulation_stats.csv") {
        veg_out.open(veg_fn);
        veg_out << "# WIDTH=" << WIDTH       << "\n"
                << "# HEIGHT=" << HEIGHT     << "\n"
                << "# MAX_TICKS=" << MAX_TICKS << "\n"
                << "# SAVE_INTERVAL=" << SAVE_INTERVAL << "\n";
        veg_out << "tick,id,x,y,age,maxAge,energy,sunEff,watEff,nutEff,decay\n";

        // write world state
        world_out.open(world_fn);
        world_out << "tick, x,y,type\n";


        stats_out.open(stats_fn);
        stats_out << "# summary stats per save\n";
        stats_out << "tick,totalEntities,energyDeaths,waterDeaths,oldAgeDeaths\n";
    }

    void save(int tick, int grassAlive,  entt::registry &reg) {
        // write vegetation state
        reg.view<Position,Age,Energy,Genes>().each(
          [&](auto id, auto &pos, auto &age, auto &e, auto &g){
            veg_out << tick << ',' << int(id) << ','
                    << pos.x << ',' << pos.y << ','
                    << age.age << ',' << age.maxAge << ','
                    << e.value << ','
                    << g.sunlightEff << ','
                    << g.waterEff << ','
                    << g.nutrientEff << ','
                    << g.decayRate << '\n';
        });

        for(int y=0;y<HEIGHT;y++) for(int x=0;x<WIDTH;x++){
            Tile &t = at(x,y);
            world_out << tick << ',' << x << ',' << y << ','
                      << (t.type==TileType::Soil?"Soil":"Water") << '\n';
        }

        // write stats
        stats_out << tick << ','
                  << grassAlive << ','
                  << energyDeaths << ','
                  << waterDeaths  << ','
                  << oldAgeDeaths << '\n';
        // reset death counters
        //energyDeaths = waterDeaths = oldAgeDeaths = 0;
    }
};


// —— Systems ——
// compute current sunlight intensity [0..1]
float sunlight(int tick){
    int seasonalTick = tick % SEASON_LENGTH;
    // vary day length ±20% over the year
    float dayLen = DAY_LENGTH * (1.0f + 0.2f*std::sin(2*PI*seasonalTick/SEASON_LENGTH));
    float tmod   = tick % int(dayLen);
    return std::clamp(1.0f - std::abs( (tmod/dayLen)*2 - 1 ), 0.0f, 1.0f);
}

void sunlightUptake(entt::registry &reg, float sunI){
    reg.view<Energy,Genes>().each(
        [&](auto &e, auto &g){
            e.value += sunI * g.sunlightEff * 0.1f;
        });
}

void waterUptake(entt::registry &reg){
    reg.view<Position,Energy,Genes>().each(
        [&](auto &pos, auto &en, auto &g){
            Tile &t = at(pos.x,pos.y);
            float taken = std::min(t.water, g.waterEff * 0.05f);
            t.water  -= taken;
            en.value += taken;
        });
}

void soilUptake(entt::registry &reg){
    reg.view<Position,Energy,Genes>().each(
        [&](auto &pos, auto &en, auto &g){
            Tile &t = at(pos.x,pos.y);
            float taken = std::min(t.nutrient, g.nutrientEff * 0.05f);
            t.nutrient -= taken;
            en.value   += taken;
        });
}

void growAndAge(entt::registry &reg){
    std::vector<entt::entity> to_kill;
    reg.view<Position,Age,Energy>().each(
        [&](auto id, auto &pos, auto &age, auto &en){
            age.age++;
            if(en.value <= 0.0f) {
                // classify death cause
                Tile &t = at(pos.x,pos.y);
                if(t.water <= 0.0f) {
                    waterDeaths++; }
                else { energyDeaths++; }
                // return a bit of nutrient
                t.nutrient += std::max(en.value, 0.5f);
                to_kill.push_back(id);
            }
            else if(age.age >= age.maxAge) {
                oldAgeDeaths++;
                Tile &t = at(pos.x,pos.y);
                t.nutrient += std::max(en.value, 0.5f);
                to_kill.push_back(id);
            }
        });
    for(auto e: to_kill) {
        reg.destroy(e);
        grassAlive--;
    }
}

void reproduce(entt::registry &reg){
    std::vector<std::tuple<Position,Genes,int>> births;
    reg.view<Position,Age,Energy,Genes>().each(
      [&](entt::entity         entity,   // explicit, by value
            Position&            pos,
            Age&                 age,
            Energy&              en,
            Genes&               g)
      {
        if(age.age >= MATURITY_AGE && en.value >= REPRODUCE_ENERGY){
          // find random neighbor
          int dx = int(uni(rng)*3)-1, dy = int(uni(rng)*3)-1;
          int nx = pos.x+dx, ny = pos.y+dy;
          if(nx>=0 && nx<WIDTH && ny>=0 && ny<HEIGHT && at(nx,ny).type==TileType::Soil){
            // mutate genes
            Genes ng = g;
            ng.sunlightEff += gauss(rng);
            ng.waterEff    += gauss(rng);
            ng.nutrientEff += gauss(rng);
            ng.decayRate   += gauss(rng)*0.02f;
            births.emplace_back(Position{nx,ny}, ng, int(age.maxAge));
            en.value *= 0.5f; // share energy
          }
        }
      });
    for(auto &b: births){
      auto e = reg.create();
      reg.emplace<Position>(e, std::get<0>(b));
      reg.emplace<Genes>(e, std::get<1>(b));
      // offspring age=0, inherit parent's maxAge with slight variance
      int parentMax = std::get<2>(b);
      reg.emplace<Age>(e, Age{0, std::max(10, int(parentMax + gauss(rng)*5))});
      reg.emplace<Energy>(e, Energy{5.0f});
      grassAlive++;
    }
}

void rainSystem(int tick){
    if(tick % RAIN_INTERVAL == 0){
      for(auto &t: grid){
        if(t.type==TileType::Soil) t.water += RAIN_AMOUNT;
      }
    }
}

int main(){
    entt::registry reg;
    generateWorld(42);
    seedGrass(reg);
    Serializer ser;

    for(int tick=0; tick<MAX_TICKS; tick++){
      float sunI = sunlight(tick);
      sunlightUptake(reg, sunI);
      waterUptake(reg);
      soilUptake(reg);
      growAndAge(reg);
      if (grassAlive < HEIGHT*WIDTH) {
        reproduce(reg);
      }
      rainSystem(tick);

      if(tick % SAVE_INTERVAL == 0) ser.save(tick, grassAlive, reg);
    }

     std::cout<<"Simulation complete. Data -> grass_states.csv, world_states.csv, simulation_stats.csv\n";
    return 0;
}
