Shader "ParticleSimulate"
{
    Compute
    {
        // GPU particle simulation. One thread per particle: integrate alive particles (gravity + velocity),
        // age them, respawn dead ones from the emitter within a per-frame spawn budget (atomic counter), and
        // bake the CURRENT interpolated size + colour into the particle so the billboard shader needs no
        // per-emitter uniforms. State lives in a PERSISTENT storage buffer (survives across frames). Dispatched
        // via DispatchComputeCull so the writes are visible to the billboard VERTEX shader that reads the same
        // buffer.

        LocalSize(64, 1, 1);

        struct Particle
        {
            vec4 PosSize; // xyz = world position, w = current size
            vec4 Color;   // rgba = current colour (a folds in the over-life alpha; 0 => dead/invisible)
            vec4 VelLife; // xyz = velocity, w = lifetime (<= 0 => dead / unspawned)
            vec4 Age;     // x = age (seconds); yzw spare
        };

        Buffer(0) Particles
        {
            Particle u_Particles[];
        };

        Buffer(1) SpawnCounter
        {
            uint u_SpawnCount; // atomically-consumed spawn budget this frame (CPU-zeroed each frame)
        };

        PushConstant PushConstants
        {
            vec4  u_EmitterPos; // xyz = emitter world pos, w = dt
            vec4  u_Gravity;    // xyz = gravity, w = time (RNG seed)
            vec4  u_Direction;  // xyz = normalized emit dir, w = cone half-angle (radians)
            vec4  u_Params;     // x = startSpeed, y = speedVar, z = lifetime, w = lifetimeVar
            vec4  u_StartColor; // rgb + start alpha (w)
            vec4  u_EndColor;   // rgb + end alpha (w)
            vec4  u_Sizes;      // x = start size, y = end size
            uvec4 u_Counts;     // x = maxParticles, y = spawnBudget, z = enabled(0/1), w = unused
        };

        uint Hash( uint x )
        {
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return x;
        }

        float Rand( inout uint state )
        {
            state = Hash( state );
            return float( state ) * ( 1.0 / 4294967296.0 );
        }

        void main()
        {
            uint i = gl_GlobalInvocationID.x;
            if ( i >= u_Counts.x )
                return;

            Particle p  = u_Particles[i];
            float    dt = u_EmitterPos.w;

            // Integrate alive particles.
            if ( p.VelLife.w > 0.0 )
            {
                p.VelLife.xyz += u_Gravity.xyz * dt;
                p.PosSize.xyz += p.VelLife.xyz * dt;
                p.Age.x += dt;
                if ( p.Age.x >= p.VelLife.w )
                    p.VelLife.w = 0.0; // died this frame
            }

            // Respawn dead particles from the emitter, within this frame's spawn budget.
            if ( p.VelLife.w <= 0.0 )
            {
                uint slot = atomicAdd( u_SpawnCount, 1u );
                if ( u_Counts.z == 1u && slot < u_Counts.y )
                {
                    uint  rng = Hash( i * 747796405u + uint( u_Gravity.w * 1000.0 ) );
                    float u1  = Rand( rng );
                    float u2  = Rand( rng );
                    float u3  = Rand( rng );

                    float cosT  = mix( 1.0, cos( u_Direction.w ), u1 );
                    float sinT  = sqrt( max( 0.0, 1.0 - cosT * cosT ) );
                    float phi   = 6.2831853 * u2;
                    vec3  local = vec3( sinT * cos( phi ), sinT * sin( phi ), cosT );

                    vec3 axis = normalize( u_Direction.xyz + vec3( 1e-5, 0.0, 0.0 ) );
                    vec3 up   = abs( axis.y ) < 0.99 ? vec3( 0.0, 1.0, 0.0 ) : vec3( 1.0, 0.0, 0.0 );
                    vec3 tang = normalize( cross( up, axis ) );
                    vec3 bitn = cross( axis, tang );
                    vec3 dir  = normalize( tang * local.x + bitn * local.y + axis * local.z );

                    float speed = u_Params.x * ( 1.0 - u_Params.y * u3 );
                    float life  = u_Params.z * ( 1.0 - u_Params.w * Rand( rng ) );

                    p.PosSize = vec4( u_EmitterPos.xyz, u_Sizes.x );
                    p.VelLife = vec4( dir * speed, max( life, 0.01 ) );
                    p.Age     = vec4( 0.0 );
                }
                else
                {
                    p.VelLife.w = 0.0; // stay dead
                }
            }

            // Bake the current size + colour from the particle's normalized age (0..1 over its life).
            float t     = ( p.VelLife.w > 0.0 ) ? clamp( p.Age.x / p.VelLife.w, 0.0, 1.0 ) : 0.0;
            p.PosSize.w = mix( u_Sizes.x, u_Sizes.y, t );
            p.Color     = mix( u_StartColor, u_EndColor, t );
            if ( p.VelLife.w <= 0.0 )
                p.Color.a = 0.0; // dead => invisible

            u_Particles[i] = p;
        }
    }
}
