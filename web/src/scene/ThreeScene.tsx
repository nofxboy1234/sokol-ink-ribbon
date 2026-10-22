import { useEffect, useRef } from "react";
import * as THREE from "three";
import { LineMaterial } from "three/addons/lines/LineMaterial.js";
import { LineSegments2 } from "three/addons/lines/LineSegments2.js";
import { LineSegmentsGeometry } from "three/addons/lines/LineSegmentsGeometry.js";
import { NavGrid } from "../game/nav";
import type { GameStore } from "../game/store";
import type { Entity, GameState, Vec2 } from "../game/types";

type Props = {
  store: GameStore;
  tracker: { current: Vec2 };
};

type Walk = {
  path: Vec2[];
  index: number;
  target: Vec2;
  nodeId: string;
};

const inkLine = 0x1d1d1d;

const disposeObject = (object: THREE.Object3D): void => {
  object.traverse((child) => {
    if (child instanceof THREE.Mesh) {
      child.geometry.dispose();
      const material = child.material;
      if (Array.isArray(material)) material.forEach((entry) => entry.dispose());
      else material.dispose();
    }
    if (child instanceof THREE.LineSegments) {
      child.geometry.dispose();
      const material = child.material;
      if (Array.isArray(material)) material.forEach((entry) => entry.dispose());
      else material.dispose();
    }
  });
};

const edgedBox = (
  width: number,
  height: number,
  depth: number,
  color: number,
  edgeColor = inkLine,
  edgeOpacity = 1,
): THREE.Group => {
  const group = new THREE.Group();
  const geometry = new THREE.BoxGeometry(width, height, depth);
  const material = new THREE.MeshStandardMaterial({ color, roughness: 0.72, metalness: 0.02 });
  const mesh = new THREE.Mesh(geometry, material);
  mesh.castShadow = true;
  mesh.receiveShadow = true;
  group.add(mesh);
  const edges = new THREE.LineSegments(
    new THREE.EdgesGeometry(geometry),
    new THREE.LineBasicMaterial({ color: edgeColor, transparent: edgeOpacity < 1, opacity: edgeOpacity }),
  );
  group.add(edges);
  return group;
};

const ringMesh = (radius: number, color: number): THREE.Mesh<THREE.RingGeometry, THREE.MeshBasicMaterial> => {
  const geometry = new THREE.RingGeometry(radius * 0.72, radius, 48);
  const material = new THREE.MeshBasicMaterial({
    color,
    transparent: true,
    opacity: 0.6,
    side: THREE.DoubleSide,
    depthWrite: false,
  });
  const mesh = new THREE.Mesh(geometry, material);
  mesh.rotation.x = -Math.PI / 2;
  mesh.position.y = 0.012;
  return mesh;
};

export function ThreeScene({ store, tracker }: Props) {
  const hostRef = useRef<HTMLDivElement>(null);
  const level = store.level;

  useEffect(() => {
    const host = hostRef.current;
    if (!host) return;

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setSize(host.clientWidth, host.clientHeight);
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    host.appendChild(renderer.domElement);

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0xfdfdfd);

    const camera = new THREE.PerspectiveCamera(42, 1, 0.1, 120);

    const hemisphere = new THREE.HemisphereLight(0xffffff, 0xdde5ef, 1.45);
    scene.add(hemisphere);

    const sun = new THREE.DirectionalLight(0xffffff, 1.85);
    sun.position.set(7, 14, 8);
    sun.castShadow = true;
    sun.shadow.mapSize.set(2048, 2048);
    sun.shadow.camera.left = -20;
    sun.shadow.camera.right = 20;
    sun.shadow.camera.top = 20;
    sun.shadow.camera.bottom = -20;
    sun.shadow.camera.near = 1;
    sun.shadow.camera.far = 40;
    sun.shadow.bias = -0.0004;
    scene.add(sun);

    const fill = new THREE.DirectionalLight(0xffffff, 0.35);
    fill.position.set(-8, 6, -6);
    scene.add(fill);

    const floor = new THREE.Mesh(
      new THREE.PlaneGeometry(90, 70),
      new THREE.MeshStandardMaterial({ color: 0xfdfdfd, roughness: 1, metalness: 0 }),
    );
    floor.rotation.x = -Math.PI / 2;
    floor.receiveShadow = true;
    scene.add(floor);

    const lineMaterial = new LineMaterial({ color: inkLine, linewidth: 2.2 });
    lineMaterial.resolution.set(host.clientWidth, host.clientHeight);

    const wallPoints: number[] = [];
    for (const wall of level.walls) {
      wallPoints.push(wall.a.x, 0.01, wall.a.z, wall.b.x, 0.01, wall.b.z);
      wallPoints.push(wall.a.x, 0.01, wall.a.z, wall.a.x, 2.5, wall.a.z);
      wallPoints.push(wall.b.x, 0.01, wall.b.z, wall.b.x, 2.5, wall.b.z);
    }
    const wallGeometry = new LineSegmentsGeometry();
    wallGeometry.setPositions(wallPoints);
    const walls = new LineSegments2(wallGeometry, lineMaterial);
    scene.add(walls);

    const doorMeshes = new Map<string, { pivot: THREE.Group; open: () => boolean }>();
    for (const door of level.doors) {
      const pivot = new THREE.Group();
      pivot.position.set(door.hinge.x, 0, door.hinge.z);
      const panel = edgedBox(door.width, 2.35, 0.09, 0xffffff, inkLine, 0.9);
      panel.position.set(door.width / 2, 1.175, 0);
      pivot.add(panel);

      const handle = new THREE.Mesh(
        new THREE.SphereGeometry(0.055, 16, 12),
        new THREE.MeshStandardMaterial({ color: 0xd9a441, roughness: 0.35, metalness: 0.5 }),
      );
      handle.position.set(door.width - 0.22, 1.04, 0.1);
      pivot.add(handle);
      scene.add(pivot);

      doorMeshes.set(door.id, {
        pivot,
        open: () => Boolean(store.getSnapshot().doors[door.id]?.open),
      });
    }

    const entityGroups = new Map<string, THREE.Group>();
    const floating: { object: THREE.Object3D; baseY: number; phase: number }[] = [];

    const makeItem = (entity: Entity, radius: number): THREE.Group => {
      const group = new THREE.Group();
      const item = entity.itemId ? level.items[entity.itemId] : undefined;
      const color = item?.color ?? 0x4ba1f1;
      const sphere = new THREE.Mesh(
        new THREE.SphereGeometry(radius, 32, 24),
        new THREE.MeshStandardMaterial({ color, roughness: 0.45, metalness: 0.02 }),
      );
      sphere.position.y = radius + 0.08;
      sphere.castShadow = true;
      sphere.receiveShadow = true;
      group.add(sphere);
      const halo = ringMesh(radius * 1.35, color);
      halo.material.opacity = 0.18;
      group.add(halo);
      floating.push({ object: sphere, baseY: radius + 0.08, phase: entity.id.length * 0.7 });
      group.position.set(entity.pos.x, 0, entity.pos.z);
      return group;
    };

    const makeKey = (entity: Entity): THREE.Group => {
      const group = new THREE.Group();
      const material = new THREE.MeshStandardMaterial({ color: 0xd9a441, roughness: 0.3, metalness: 0.55 });
      const bow = new THREE.Mesh(new THREE.TorusGeometry(0.12, 0.035, 16, 32), material);
      bow.rotation.x = Math.PI / 2;
      group.add(bow);
      const shaft = new THREE.Mesh(new THREE.BoxGeometry(0.36, 0.055, 0.055), material);
      shaft.position.x = 0.24;
      group.add(shaft);
      const tooth = new THREE.Mesh(new THREE.BoxGeometry(0.06, 0.12, 0.055), material);
      tooth.position.set(0.38, -0.05, 0);
      group.add(tooth);
      group.position.set(entity.pos.x, 0.34, entity.pos.z);
      group.rotation.y = 0.6;
      const halo = ringMesh(0.34, 0xd9a441);
      halo.position.set(0, -0.328, 0);
      group.add(halo);
      return group;
    };

    const makeStation = (entity: Entity): THREE.Group => {
      const group = new THREE.Group();
      if (entity.station === "typewriter") {
        const desk = edgedBox(1.7, 0.82, 1.05, 0xfdfdfd);
        desk.position.y = 0.41;
        group.add(desk);
        const body = edgedBox(0.86, 0.26, 0.55, 0x1d1d1d, inkLine, 0.8);
        body.position.set(0, 0.95, 0);
        group.add(body);
        const paper = edgedBox(0.46, 0.02, 0.34, 0x4ba1f1, inkLine, 0.5);
        paper.position.set(0, 1.1, -0.05);
        paper.rotation.x = -0.12;
        group.add(paper);
        const keys = edgedBox(0.68, 0.035, 0.34, 0xfdfdfd, inkLine, 0.4);
        keys.position.set(0, 0.9, 0.38);
        keys.rotation.x = 0.08;
        group.add(keys);
      } else {
        const pedestal = edgedBox(1.05, 0.9, 0.75, 0xfdfdfd);
        pedestal.position.y = 0.45;
        group.add(pedestal);
        const book = edgedBox(0.72, 0.1, 0.52, 0x4ba1f1, inkLine, 0.6);
        book.position.y = 0.97;
        book.rotation.x = 0.12;
        group.add(book);
        const pages = edgedBox(0.66, 0.06, 0.46, 0xfdfdfd, inkLine, 0.35);
        pages.position.set(0.02, 1.03, 0.01);
        pages.rotation.x = 0.12;
        group.add(pages);
      }
      group.position.set(entity.pos.x, 0, entity.pos.z);
      return group;
    };

    for (const entity of level.entities) {
      if (entity.kind === "door") continue;
      let group: THREE.Group;
      if (entity.itemId === "brass-key") group = makeKey(entity);
      else if (entity.kind === "item") group = makeItem(entity, entity.itemId === "ink-ribbon" ? 0.38 : 0.34);
      else group = makeStation(entity);
      scene.add(group);
      entityGroups.set(entity.id, group);
    }

    const player = new THREE.Group();
    const capsule = new THREE.Mesh(
      new THREE.CapsuleGeometry(0.42, 1.15, 12, 28),
      new THREE.MeshStandardMaterial({ color: 0x4ba1f1, roughness: 0.5, metalness: 0.02 }),
    );
    capsule.position.y = 0.99;
    capsule.castShadow = true;
    capsule.receiveShadow = true;
    player.add(capsule);
    const playerRing = ringMesh(0.62, 0x4ba1f1);
    playerRing.material.opacity = 0.32;
    player.add(playerRing);
    scene.add(player);

    const destination = ringMesh(0.66, 0x3266ee);
    destination.material.opacity = 0;
    scene.add(destination);

    const motion: Vec2 = { ...store.getSnapshot().player.pos };
    let nav = new NavGrid(level, Object.fromEntries(level.doors.map((door) => [door.id, store.getSnapshot().doors[door.id]?.open ?? false])));
    let doorSignature = level.doors.map((door) => `${door.id}:${store.getSnapshot().doors[door.id]?.open}`).join("|");
    let walk: Walk | null = null;
    let arriveAt = 0;
    let currentPendingId: string | null = null;

    const syncView = (state: GameState): void => {
      for (const entity of level.entities) {
        const group = entityGroups.get(entity.id);
        if (!group) continue;
        let visible = !state.collected[entity.id];
        if (entity.itemId === "brass-key") visible = visible && Boolean(state.discovered[entity.id]);
        if (entity.station === "daybook") visible = visible && Boolean(state.discovered[entity.id]);
        group.visible = visible;
      }

      const signature = level.doors.map((door) => `${door.id}:${state.doors[door.id]?.open}`).join("|");
      if (signature !== doorSignature) {
        doorSignature = signature;
        nav = new NavGrid(
          level,
          Object.fromEntries(level.doors.map((door) => [door.id, state.doors[door.id]?.open ?? false])),
        );
      }

      if (state.pending && state.pending.nodeId !== currentPendingId) {
        currentPendingId = state.pending.nodeId;
        const path = nav.findPath(motion, state.pending.target);
        if (!path || path.length === 0) {
          queueMicrotask(() => store.failPending("There is no way through from here."));
          walk = null;
        } else {
          walk = { path, index: 1, target: state.pending.target, nodeId: state.pending.nodeId };
          if (path.length === 1) walk.index = 0;
          arriveAt = 0;
          destination.position.set(state.pending.target.x, 0.012, state.pending.target.z);
          destination.material.opacity = 0.55;
        }
      }

      if (!state.pending) {
        currentPendingId = null;
        walk = null;
      }
    };

    syncView(store.getSnapshot());
    const unsubscribe = store.subscribe(() => syncView(store.getSnapshot()));

    const resize = (): void => {
      const width = host.clientWidth;
      const height = host.clientHeight;
      renderer.setSize(width, height);
      camera.aspect = width / Math.max(1, height);
      camera.updateProjectionMatrix();
      lineMaterial.resolution.set(width, height);
    };
    resize();
    const observer = new ResizeObserver(resize);
    observer.observe(host);

    const desiredCamera = new THREE.Vector3();
    const lookAt = new THREE.Vector3();
    const smoothLook = new THREE.Vector3(motion.x, 0.9, motion.z);
    desiredCamera.set(motion.x + 5.2, 7.0, motion.z + 7.0);
    camera.position.copy(desiredCamera);
    camera.lookAt(smoothLook);

    const clock = new THREE.Clock();
    let raf = 0;
    let elapsed = 0;

    const animate = (): void => {
      raf = requestAnimationFrame(animate);
      const delta = Math.min(clock.getDelta(), 0.05);
      elapsed += delta;

      if (walk) {
        let remaining = 3.15 * delta;
        while (remaining > 0 && walk) {
          const point = walk.path[walk.index];
          const dx = point.x - motion.x;
          const dz = point.z - motion.z;
          const distance = Math.hypot(dx, dz);
          if (distance <= remaining || distance < 0.015) {
            motion.x = point.x;
            motion.z = point.z;
            remaining -= distance;
            walk.index += 1;
            if (walk.index >= walk.path.length) {
              const finished = walk;
              walk = null;
              arriveAt = elapsed + 0.12;
              void finished;
              break;
            }
          } else {
            motion.x += (dx / distance) * remaining;
            motion.z += (dz / distance) * remaining;
            remaining = 0;
          }
        }
      }

      if (arriveAt > 0 && elapsed >= arriveAt) {
        arriveAt = 0;
        destination.material.opacity = 0;
        queueMicrotask(() => store.finishPending());
      }

      player.position.set(motion.x, 0, motion.z);
      capsule.position.y = 0.99 + Math.sin(elapsed * 11) * (walk ? 0.045 : 0);
      playerRing.rotation.z = elapsed * 0.4;
      destination.rotation.z = elapsed * 0.6;
      const pulse = 1 + Math.sin(elapsed * 3.2) * 0.06;
      destination.scale.set(pulse, pulse, pulse);

      for (const entry of floating) {
        entry.object.position.y = entry.baseY + Math.sin(elapsed * 1.9 + entry.phase) * 0.035;
        if (entry.object instanceof THREE.Group) entry.object.rotation.y = 0.6 + elapsed * 0.35;
      }

      for (const door of level.doors) {
        const entry = doorMeshes.get(door.id);
        if (!entry) continue;
        const targetAngle = entry.open() ? Math.PI * 0.62 : 0;
        entry.pivot.rotation.y += (targetAngle - entry.pivot.rotation.y) * Math.min(1, delta * 4.5);
      }

      const cameraTargetX = motion.x + 5.2;
      const cameraTargetZ = motion.z + 7.0;
      const cameraEase = 1 - Math.exp(-delta * 3.2);
      camera.position.x += (cameraTargetX - camera.position.x) * cameraEase;
      camera.position.z += (cameraTargetZ - camera.position.z) * cameraEase;
      camera.position.y += (7.0 - camera.position.y) * cameraEase;

      lookAt.set(motion.x + 1.35, 0.9, motion.z - 0.2);
      smoothLook.lerp(lookAt, 1 - Math.exp(-delta * 4.2));
      camera.lookAt(smoothLook);

      tracker.current = { x: motion.x, z: motion.z };
      renderer.render(scene, camera);
    };
    raf = requestAnimationFrame(animate);

    return () => {
      cancelAnimationFrame(raf);
      observer.disconnect();
      unsubscribe();
      host.removeChild(renderer.domElement);
      disposeObject(scene);
      lineMaterial.dispose();
      renderer.dispose();
    };
  }, [level, store, tracker]);

  return <div ref={hostRef} className="scene-host" />;
}
