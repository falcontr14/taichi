// disabling to support old CPUs without necessary simd inst...
#if (0)
#include <taichi/lang.h>
#include <taichi/testing.h>
#include <numeric>
#include <taichi/visual/gui.h>
#include <taichi/system/threading.h>
#include <Eigen/Dense>
#include <taichi/math/eigen.h>
#include <Eigen/StdVector>

TLANG_NAMESPACE_BEGIN

static TC_FORCE_INLINE __m128 make_float4(float32 a,
                                          float32 b,
                                          float32 c,
                                          float32 d) {
  return _mm_set_ps(d, c, b, a);
}

TC_FORCE_INLINE __m128 make_float3(float a, float b, float c) {
  return make_float4(a, b, c, 0);
}

// clang-format off
TC_ALIGNED(64) const static __m128 grid_pos_offset_[27] = {
    make_float3(0, 0, 0),
    make_float3(0, 0, 1),
    make_float3(0, 0, 2),
    make_float3(0, 1, 0),
    make_float3(0, 1, 1),
    make_float3(0, 1, 2),
    make_float3(0, 2, 0),
    make_float3(0, 2, 1),
    make_float3(0, 2, 2),
    make_float3(1, 0, 0),
    make_float3(1, 0, 1),
    make_float3(1, 0, 2),
    make_float3(1, 1, 0),
    make_float3(1, 1, 1),
    make_float3(1, 1, 2),
    make_float3(1, 2, 0),
    make_float3(1, 2, 1),
    make_float3(1, 2, 2),
    make_float3(2, 0, 0),
    make_float3(2, 0, 1),
    make_float3(2, 0, 2),
    make_float3(2, 1, 0),
    make_float3(2, 1, 1),
    make_float3(2, 1, 2),
    make_float3(2, 2, 0),
    make_float3(2, 2, 1),
    make_float3(2, 2, 2),
};
// clang-format on

TC_TEST("cast_int") {
  __m128 f = _mm_set_ps(0.1f, 0.4f, 0.6f, 0.7f);
  union {
    __m128i i;
    int v[4];
  };
  i = _mm_cvtps_epi32(f);  // round to nearest, not floor....
  for (int j = 0; j < 4; j++) {
    TC_P(v[j]);
  }
}

struct MPMContext {
 public:
  struct Particle {
    real J;
    Vector3 pos, v;
    Matrix3 C;

    Particle() {
      pos = Vector3::rand() * 0.5_f + Vector3(0.25_f);
      v = Vector3::rand();
      C = Matrix3::rand();
      J = rand<float32>();
    }
  };

  struct ParticleEigen {
    // EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    Eigen::Matrix<real, 3, 3> C;
    Eigen::Matrix<real, 3, 1> pos, v;
    real J;
    ParticleEigen() {
    }

    ParticleEigen(Particle &p) {
      pos = to_eigen(p.pos);
      v = to_eigen(p.v);
      C = to_eigen(p.C);
      J = p.J;
    }
  };

  int n_particles;  // num particles
  static constexpr real mass = 2.0_f;
  static constexpr real vol = 3.0_f;
  static constexpr int n = 7;
  static constexpr real dx = 1.0_f / n;
  static constexpr real inv_dx = 1.0_f / dx;
  static constexpr real dt = 1e-1_f;

  using Grid = Vector4[n][n][n];
  Grid grid;
  using EigenGrid = Eigen::Matrix<real, 4, 1>[n][n][n];
  EigenGrid eigen_grid;

  void clear_grid() {
    std::memset(grid, 0, sizeof(grid));
    std::memset(eigen_grid, 0, sizeof(eigen_grid));
  }

  std::vector<Particle> particles;
  std::vector<ParticleEigen> eigen_particles;

  MPMContext(int n_particles) : n_particles(n_particles) {
    particles.resize(n_particles);
    eigen_particles.resize(n_particles);
    for (int i = 0; i < n_particles; i++) {
      particles[i] = Particle();
      eigen_particles[i] = ParticleEigen(particles[i]);
    }
    clear_grid();
  }

  void p2g() {
    constexpr int T = 3;
    TC_WARN_IF(T != 3, "T is not 3");

    for (int p_i = 0; p_i < n_particles; p_i++) {
      using Vec = Vector3;
      auto &p = particles[p_i];
      Vector3i base_coord = (p.pos * inv_dx - Vector3(0.5_f)).cast<int>();
      auto fx = p.pos * inv_dx - base_coord.cast<real>();
      // Quadratic kernels  [http://mpm.graphics   Eqn. 123, with x=fx,
      // fx-1,fx-2]
      Vec w[3]{Vec(0.5) * sqr(Vec(1.5) - fx), Vec(0.75) - sqr(fx - Vec(1.0)),
               Vec(0.5) * sqr(fx - Vec(0.5))};
      real J = p.J;
      auto stress =  // Cauchy stress times dt and inv_dx
          -4 * inv_dx * inv_dx * dt * vol * Matrix3(J - 1);
      auto affine = stress + mass * p.C;

      // Scatter to grid
      for (int i = 0; i < T; i++) {
        for (int j = 0; j < T; j++) {
          for (int k = 0; k < T; k++) {
            auto dpos = (Vec(i, j, k) - fx) * dx;
            Vector4 mv(p.v * mass, mass);  // translational momentum
            grid[base_coord.x + i][base_coord.y + j][base_coord.z + k] +=
                w[i].x * w[j].y * w[k].z * (mv + Vector4(affine * dpos, 0));
          }
        }
      }
    }
  }

  void p2g_eigen() {
    constexpr int T = 3;
    TC_WARN_IF(T != 3, "T is not 3");

    for (int p_i = 0; p_i < n_particles; p_i++) {
      using Vec = Eigen::Matrix<real, 3, 1>;
      auto sqr = [](const Vec &vec) -> Vec {
        Vec ret;
        for (int i = 0; i < 3; i++) {
          ret(i) = vec(i) * vec(i);
        }
        return ret;
      };
      auto &p = eigen_particles[p_i];
      Eigen::Matrix<int, 3, 1> base_coord =
          (p.pos * inv_dx - Vec::Ones() * 0.5_f).cast<int>();
      Vec fx = p.pos * inv_dx - base_coord.cast<real>();

      Vec w[3]{0.5f * sqr(Vec::Ones() * 1.5f - fx),
               Vec::Ones() * 0.75f - sqr(fx - Vec::Ones()),
               0.5f * sqr(fx - Vec::Ones() * 0.5f)};
      real J = p.J;
      auto stress =  // Cauchy stress times dt and inv_dx
          -4 * inv_dx * inv_dx * dt * vol * (J - 1) *
          Eigen::Matrix<real, 3, 3>::Identity();
      Eigen::Matrix<real, 3, 3> affine = stress + mass * p.C;

      // Scatter to grid
      for (int i = 0; i < T; i++) {
        for (int j = 0; j < T; j++) {
          for (int k = 0; k < T; k++) {
            Vec dpos = (Vec(i, j, k) - fx) * dx;
            Eigen::Matrix<real, 4, 1> contrib;
            for (int d = 0; d < 3; d++) {
              contrib(d) = mass * p.v(d) + (affine * dpos)(d);
            }
            contrib(3) = mass;
            eigen_grid[base_coord(0) + i][base_coord(1) + j]
                      [base_coord(2) + k] +=
                w[i](0) * w[j](1) * w[k](2) * contrib;
          }
        }
      }
    }
  }

  using Vector = Vector3f;
  using Matrix = Matrix3f;
  using Vectori = Vector3i;

  struct MLSMPMFastKernel32 {
    static constexpr int dim = 3;
    TC_ALIGNED(64) __m128 kernels[3][3];

    TC_FORCE_INLINE MLSMPMFastKernel32(const __m128 &pos, real inv_delta_x) {
      __m128 p_fract = _mm_sub_ps(pos, _mm_set1_ps(0.5f));
      TC_ALIGNED(64) __m128 w_cache[dim];
      for (int k = 0; k < dim; k++) {
        __m128 t = _mm_sub_ps(_mm_set1_ps(p_fract[k]),
                              make_float4(-0.5f, 0.5f, 1.5f, 0.0f));
        __m128 tt = _mm_mul_ps(t, t);
        w_cache[k] = _mm_fmadd_ps(
            make_float4(0.5f, -1.0f, 0.5f, 0.0f), tt,
            _mm_fmadd_ps(make_float4(-1.5f, 0.0f, 1.5f, 0.0f), t,
                         make_float4(1.125f, 0.75f, 1.125f, 0.0f)));
      }
      for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
          kernels[i][j] = _mm_mul_ps(_mm_set1_ps(w_cache[0][i] * w_cache[1][j]),
                                     w_cache[2]);
        }
      }
    }
  };

  TC_FORCE_INLINE static int get_stencil_start(real x) {
    return int(x - 0.5f);
  }

#define broadcast(s, i) _mm_shuffle_ps((s), (s), 0x55 * (i))
  void p2g_intrinsics() {
    const auto inv_delta_x = 1.0_f / dx;
    const real delta_t = dt;
    __m128 S = _mm_set1_ps(-4.0_f * inv_delta_x * delta_t);
    for (int p_i = 0; p_i < n_particles; p_i++) {
      Particle &p = particles[p_i];

      // Note, pos is magnified grid pos
      __m128 pos_ = _mm_mul_ps(p.pos.v, _mm_set1_ps(inv_delta_x));

      Vectori grid_base_pos(get_stencil_start(pos_[0]),
                            get_stencil_start(pos_[1]),
                            get_stencil_start(pos_[2]));
      auto base_offset =
          grid_base_pos[0] * n * n + grid_base_pos[1] * n + grid_base_pos[2];
      Vector grid_base_pos_f = Vector(grid_base_pos);

      MLSMPMFastKernel32 kernel(_mm_sub_ps(pos_, grid_base_pos_f), inv_delta_x);
      const __m128(&kernels)[3][3] = kernel.kernels;
      const __m128 v = p.v.v;
      __m128 mass_ = _mm_set1_ps(mass);
      // Note, apic_b has delta_x issue
      const Matrix3f apic_b_inv_d_mass = (-mass * dx) * p.C;
      const __m128 mass_v = _mm_mul_ps(_mm_set1_ps(mass), v);

      auto stress = -vol * Matrix3(p.J - 1);

      __m128 delta_t_tmp_force_[3];
      auto &delta_t_tmp_force = reinterpret_cast<Matrix &>(delta_t_tmp_force_);
      for (int i = 0; i < 3; i++) {
        delta_t_tmp_force_[i] = _mm_mul_ps(_mm_set1_ps(delta_t), stress[i]);
      }

      __m128 rela_pos = _mm_sub_ps(pos_, grid_base_pos_f);
      __m128 affine[3];

      for (int i = 0; i < 3; i++)
        affine[i] = _mm_fmadd_ps(stress[i], S, apic_b_inv_d_mass[i]);
#define LOOP(node_id)                                                       \
  {                                                                         \
    __m128 dpos = _mm_sub_ps(rela_pos, grid_pos_offset_[node_id]);          \
    __m128 weight =                                                         \
        _mm_set1_ps(kernels[node_id / 9][node_id / 3 % 3][node_id % 3]);    \
    __m128 affine_prod = _mm_fmadd_ps(                                      \
        affine[2], broadcast(dpos, 2),                                      \
        _mm_fmadd_ps(affine[1], broadcast(dpos, 1),                         \
                     _mm_fmadd_ps(affine[0], broadcast(dpos, 0), mass_v))); \
    __m128 contrib = _mm_blend_ps(mass_, affine_prod, 0x7);                 \
    __m128 delta = _mm_mul_ps(weight, contrib);                             \
    auto &g = grid[0][0][node_id / 9 * n * n + node_id / 3 % 3 * n +        \
                         node_id % 3 + base_offset];                        \
    g = _mm_add_ps(g, delta);                                               \
  }
      TC_REPEAT27(LOOP);
#undef LOOP
    }
  }
#undef broadcast
};

TC_TEST("simd_mpm_baselines") {
  {
    MPMContext context(128);
    context.p2g();

    MPMContext::Grid grid_gt;
    std::memcpy(&grid_gt[0][0][0], &context.grid[0][0][0],
                sizeof(MPMContext::Grid));
    context.clear_grid();
    context.p2g_intrinsics();
    context.p2g_eigen();

    for (int i = 0; i < context.n; i++) {
      for (int j = 0; j < context.n; j++) {
        for (int k = 0; k < context.n; k++) {
          for (int d = 0; d < 4; d++) {
            // TC_INFO("{} {} {} {} , {}", i, j, k, d, grid_gt[i][j][k][d]);
            TC_CHECK_EQUAL(grid_gt[i][j][k][d], context.grid[i][j][k][d],
                           1e-3_f);
            TC_CHECK_EQUAL(grid_gt[i][j][k][d], context.eigen_grid[i][j][k](d),
                           1e-3_f);
          }
        }
      }
    }
  }

  MPMContext context(4 * 1024 * 1024);
  int N = 4;
  for (int i = 0; i < N; i++) {
    TC_TIME(context.p2g());
  }

  context.clear_grid();
  for (int i = 0; i < N; i++) {
    TC_TIME(context.p2g_intrinsics());
  }

  for (int i = 0; i < N; i++) {
    TC_TIME(context.p2g_eigen());
  }
};

// TODO: shuffled inputs?

TC_TEST("simd_mpm") {
  initialize_benchmark();
  int n_particles = 4 * 1024 * 1024;
  MPMContext context(n_particles);
  int n_grid = context.n;
  context.p2g();

  CoreState::set_trigger_gdb_when_crash(true);
  Program prog(Arch::x86_64);
  prog.config.max_vector_width = 4;
  prog.config.gcc_version = -2;
  prog.config.serial_schedule = true;
  prog.config.force_vectorized_global_load = true;
  prog.config.force_vectorized_global_store = true;
  // prog.config.extra_flags = "-fno-slp-vectorize";

  Global(g_J, f32);
  Global(tmp, i32);
  Global(tmp1, f32);
  auto ind = Index(0);
  constexpr int dim = 3;

  Vector grid(DataType::f32, dim + 1);
  Vector g_pos(DataType::f32, dim);
  Vector g_v(DataType::f32, dim);
  Vector g_C(DataType::f32, dim, dim);

  layout([&]() {
    root.dense(ind, n_particles).place(tmp);
    root.dense(ind, n_particles).place(tmp1);
    root.dense(ind, n_particles).place(g_J);
    for (int i = 0; i < dim; i++) {
      root.dense(ind, n_particles).place(g_pos(i));
      root.dense(ind, n_particles).place(g_v(i));
      for (int j = 0; j < dim; j++) {
        root.dense(ind, n_particles).place(g_C(i, j));
      }
    }
    root.dense(ind, bit::least_pot_bound(n_grid * n_grid * n_grid))
        .place(grid(0), grid(1), grid(2), grid(3));
  });

  auto &p2g = kernel([&]() {
    Declare(p_i);
    Vectorize(4);
    For(p_i, 0, n_particles, [&]() {
      auto mass = context.mass;
      auto vol = context.vol;
      auto dx = context.dx;
      auto inv_dx = context.inv_dx;
      auto dt = context.dt;

      auto v = Eval(g_v[p_i]);
      auto pos = Eval(g_pos[p_i]);
      auto J = Eval(g_J[p_i]);

      Vector v4(4);
      for (int i = 0; i < dim; i++) {
        v4(i) = v(i);
      }
      v4(3) = real(1);

      Vector base_coord = (inv_dx * pos - 0.5_f).cast_elements<int>();
      auto base_offset = Eval(base_coord(0) * (n_grid * n_grid) +
                              base_coord(1) * (n_grid) + base_coord(2));
      Vector fx = inv_dx * pos - base_coord.cast_elements<real>();

      Vector w[3];
      w[0] = 0.5_f * sqr(1.5_f - fx);
      w[1] = 0.75_f - sqr(fx - 1.0_f);
      w[2] = 0.5_f * sqr(fx - 0.5_f);

      Vector fx4(4);
      for (int i = 0; i < dim; i++) {
        fx4(i) = fx(i);
      }
      fx4(3) = real(0);

      Matrix stress(dim, dim);
      for (int i = 0; i < dim; i++) {
        for (int j = 0; j < dim; j++) {
          if (i == j) {
            stress(i, j) = J - real(1);
          } else {
            stress(i, j) = real(0);
          }
        }
      }

      auto scaled_stress = Eval((-4 * inv_dx * inv_dx * dt * vol) * stress);

      Matrix affine_ = dx * (scaled_stress + mass * g_C[p_i]);
      Matrix affine__(4, 3);
      for (int i = 0; i < dim; i++) {
        for (int j = 0; j < dim; j++) {
          affine__(i, j) = affine_(i, j);
        }
        affine__(dim, i) = real(0);
      }
      Matrix affine(4, 3);
      int slp = 4;
      SLP(slp);
      for (int j = 0; j < dim; j++) {
        for (int i = 0; i < dim + 1; i++) {
          affine(i, j) = Eval(affine__(i, j));
        }
      }

      constexpr int T = 3;
      TC_WARN_IF(T != 3, "T is not 3");

      auto mv = Eval(mass * v4);

      auto contrib0 = Eval(mv - Eval(affine * fx));
      for (int i = 0; i < T; i++) {
        SLP(1);
        auto weight0 = Eval(w[i](0));
        SLP(slp);
        auto contrib1 = Eval(contrib0);
        for (int j = 0; j < T; j++) {
          SLP(1);
          auto weight1 = Eval(weight0 * w[j](1));
          SLP(slp);
          auto contrib2 = Eval(contrib1);
          for (int k = 0; k < T; k++) {
            SLP(1);
            auto weight2 = Eval(weight1 * w[k](2));
            SLP(slp);
            auto contrib2 = Eval(contrib1 + real(k) * affine.col(2));
            grid[base_offset + (i * n_grid * n_grid + j * n_grid + k)] +=
                weight2 * contrib2;
          }
          SLP(slp);
          contrib1.set(Eval(contrib1 + affine.col(1)));
        }
        SLP(slp);
        contrib0.set(Eval(contrib0 + affine.col(0)));
      }
    });
  });

  auto initialize_data = [&] {
    for (int p = 0; p < n_particles; p++) {
      g_J.val<float32>(p) = context.particles[p].J;
      for (int i = 0; i < dim; i++) {
        g_pos(i).val<float32>(p) = context.particles[p].pos[i];
        g_v(i).val<float32>(p) = context.particles[p].v[i];
        for (int j = 0; j < dim; j++) {
          g_C(i, j).val<float32>(p) = context.particles[p].C[j][i];
        }
      }
    }
  };
  TC_TIME(initialize_data());

  TC_P(taichi::PID::get_pid());
  TC_TIME(p2g());

  /*
  for (int i = 0; i < n_particles; i++) {
    TC_P(tmp.val<int32>(i));
  }
  for (int i = 0; i < n_particles; i++) {
    TC_P(tmp1.val<float32>(i));
  }
  return;
  */

  auto tolerance = 1e-4_f * std::sqrt((real)n_particles);
  for (int i = 0; i < context.n; i++) {
    for (int j = 0; j < context.n; j++) {
      for (int k = 0; k < context.n; k++) {
        for (int d = 0; d < 4; d++) {
          if (std::abs(
                  grid(d).val<float32>(i * n_grid * n_grid + j * n_grid + k) -
                  context.grid[i][j][k][d]) > tolerance) {
            TC_WARN("{},{},{} {}", i, j, k, d);
          }
          TC_CHECK_EQUAL(
              grid(d).val<float32>(i * n_grid * n_grid + j * n_grid + k),
              context.grid[i][j][k][d], tolerance);
        }
      }
    }
  }
  for (int i = 0; i < 10; i++)
    TC_TIME(p2g());
};

TLANG_NAMESPACE_END
#endif
