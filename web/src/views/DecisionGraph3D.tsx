import { useEffect, useRef, type JSX } from "react";
import * as THREE from "three";
import { layoutGraph3D } from "../game/layout";
import type { Decision, DecisionGraph, DecisionKind } from "../game/types";

export type DecisionGraph3DProps = {
  graph: DecisionGraph;
  selectedId: string | null;
  onSelect: (nodeId: string) => void;
};

type NodeStyle = {
  color: number;
  radius: number;
  rim: boolean;
  glow: number;
};

const BACKDROP = 0xfbfbfd;
const PLAYER_COLOR = 0x3266ee;
const ACTION_COLOR = 0x589be6;
const INVENTORY_COLOR = 0x9da0af;
const EDGE_COLOR = 0xc9cedb;
const LABEL_COLOR = "#9da0af";
const LABEL_SELECTED_COLOR = "#3266ee";
const LABEL_FONT_PX = 26;
const LABEL_PADDING_PX = 14;
const LABEL_HEIGHT = 0.15;
const LABEL_FONT = `500 ${LABEL_FONT_PX}px ui-sans-serif, system-ui, -apple-system, "Segoe UI", Roboto, Helvetica, Arial, sans-serif`;
const RIM_SCALE = 1.1;
const SELECT_SCALE = 1.35;
const DRAG_THRESHOLD_PX = 4;
const SPRING_RATE = 9;
const SCALE_RATE = 12;
const IDLE_SPIN = 0.07;
const MIN_POLAR = 0.25;
const MAX_POLAR = Math.PI - 0.25;
const MIN_ZOOM = 0.55;
const MAX_ZOOM = 2.4;

const NODE_STYLE: Record<DecisionKind, NodeStyle> = {
  player: { color: PLAYER_COLOR, radius: 0.3, rim: false, glow: PLAYER_COLOR },
  move: { color: 0xffffff, radius: 0.21, rim: true, glow: PLAYER_COLOR },
  take: { color: ACTION_COLOR, radius: 0.2, rim: false, glow: ACTION_COLOR },
  use: { color: ACTION_COLOR, radius: 0.2, rim: false, glow: ACTION_COLOR },
  read: { color: ACTION_COLOR, radius: 0.2, rim: false, glow: ACTION_COLOR },
  open: { color: ACTION_COLOR, radius: 0.2, rim: false, glow: ACTION_COLOR },
  unlock: { color: ACTION_COLOR, radius: 0.2, rim: false, glow: ACTION_COLOR },
  inventory: { color: INVENTORY_COLOR, radius: 0.24, rim: false, glow: INVENTORY_COLOR },
};

type Label = {
  sprite: THREE.Sprite;
  material: THREE.SpriteMaterial;
  texture: THREE.CanvasTexture;
  canvas: HTMLCanvasElement;
  context: CanvasRenderingContext2D;
  text: string;
  color: string;
};

type NodeView = {
  id: string;
  kind: DecisionKind;
  radius: number;
  mesh: THREE.Mesh<THREE.SphereGeometry, THREE.MeshStandardMaterial>;
  rim: THREE.Mesh<THREE.SphereGeometry, THREE.MeshBasicMaterial> | null;
  label: Label;
  current: THREE.Vector3;
  target: THREE.Vector3;
  scale: number;
  targetScale: number;
};

type Controls = {
  setGraph: (graph: DecisionGraph) => void;
  setSelected: (nodeId: string | null) => void;
  project: (nodeId: string) => { x: number; y: number } | null;
  dispose: () => void;
};

export function DecisionGraph3D({ graph, selectedId, onSelect }: DecisionGraph3DProps): JSX.Element {
  const hostRef = useRef<HTMLDivElement>(null);
  const controlsRef = useRef<Controls | null>(null);
  const onSelectRef = useRef(onSelect);
  const selectedRef = useRef<string | null>(selectedId);

  useEffect(() => {
    onSelectRef.current = onSelect;
  }, [onSelect]);

  useEffect(() => {
    selectedRef.current = selectedId;
    controlsRef.current?.setSelected(selectedId);
  }, [selectedId]);

  useEffect(() => {
    const host = hostRef.current;
    if (!host) return;

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false, powerPreference: "high-performance" });
    renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.toneMapping = THREE.NoToneMapping;

    const canvas = renderer.domElement;
    canvas.style.display = "block";
    canvas.style.width = "100%";
    canvas.style.height = "100%";
    canvas.style.touchAction = "none";
    canvas.style.cursor = "grab";
    host.appendChild(canvas);

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(BACKDROP);

    const camera = new THREE.PerspectiveCamera(42, 1, 0.1, 400);

    const hemisphere = new THREE.HemisphereLight(0xffffff, 0xebeff7, 1.2);
    scene.add(hemisphere);

    const key = new THREE.DirectionalLight(0xffffff, 1.55);
    key.position.set(4.5, 7, 6);
    scene.add(key);

    const fill = new THREE.DirectionalLight(0xffffff, 0.45);
    fill.position.set(-6, 2, -5);
    scene.add(fill);

    const group = new THREE.Group();
    scene.add(group);

    const sphereGeometry = new THREE.SphereGeometry(1, 32, 24);
    const rimMaterial = new THREE.MeshBasicMaterial({ color: PLAYER_COLOR, side: THREE.BackSide });
    const edgeMaterial = new THREE.LineBasicMaterial({
      color: EDGE_COLOR,
      transparent: true,
      opacity: 0.78,
      depthWrite: false,
    });

    const measureCanvas = document.createElement("canvas");
    const measureContext = measureCanvas.getContext("2d");
    if (!measureContext) throw new Error("Canvas 2D is unavailable");
    const labelDpr = Math.min(2.5, Math.max(1, window.devicePixelRatio || 1));

    const views = new Map<string, NodeView>();
    const viewByMesh = new Map<THREE.Object3D, NodeView>();
    const orbit = {
      azimuth: 0.62,
      polar: 1.16,
      zoom: 1,
      target: new THREE.Vector3(0, 0.08, 0),
    };

    let edgeGeometry: THREE.BufferGeometry | null = null;
    let edgeMesh: THREE.LineSegments<THREE.BufferGeometry, THREE.LineBasicMaterial> | null = null;
    let edgeAttribute: THREE.BufferAttribute | null = null;
    let edgeValues: Float32Array | null = null;
    let edgePairs: Array<[NodeView, NodeView]> = [];
    let fitRadius = 4;
    let activeId: string | null = selectedRef.current;
    let raf = 0;

    const drawLabel = (label: Label, text: string, color: string) => {
      const cssHeight = Math.ceil(LABEL_FONT_PX * 1.35);
      measureContext.font = LABEL_FONT;
      const textWidth = Math.ceil(measureContext.measureText(text).width) + LABEL_PADDING_PX * 2;
      const cssWidth = Math.max(cssHeight, textWidth);
      label.canvas.width = Math.round(cssWidth * labelDpr);
      label.canvas.height = Math.round(cssHeight * labelDpr);
      label.context.setTransform(labelDpr, 0, 0, labelDpr, 0, 0);
      label.context.clearRect(0, 0, cssWidth, cssHeight);
      label.context.font = LABEL_FONT;
      label.context.fillStyle = color;
      label.context.textAlign = "center";
      label.context.textBaseline = "middle";
      label.context.fillText(text, cssWidth / 2, cssHeight / 2);
      label.texture.needsUpdate = true;
      label.text = text;
      label.color = color;
      label.sprite.scale.set(LABEL_HEIGHT * (cssWidth / cssHeight), LABEL_HEIGHT, 1);
    };

    const createLabel = (text: string, color: string): Label => {
      const canvasEl = document.createElement("canvas");
      const context = canvasEl.getContext("2d");
      if (!context) throw new Error("Canvas 2D is unavailable");
      const texture = new THREE.CanvasTexture(canvasEl);
      texture.colorSpace = THREE.SRGBColorSpace;
      texture.minFilter = THREE.LinearFilter;
      texture.magFilter = THREE.LinearFilter;
      texture.generateMipmaps = false;
      const material = new THREE.SpriteMaterial({
        map: texture,
        transparent: true,
        depthTest: false,
        depthWrite: false,
        toneMapped: false,
      });
      const sprite = new THREE.Sprite(material);
      sprite.renderOrder = 3;
      const label: Label = {
        sprite,
        material,
        texture,
        canvas: canvasEl,
        context,
        text: "",
        color: "",
      };
      drawLabel(label, text, color);
      return label;
    };

    const createView = (node: Decision, spawn: THREE.Vector3): NodeView => {
      const style = NODE_STYLE[node.kind];
      const material = new THREE.MeshStandardMaterial({
        color: style.color,
        roughness: 0.58,
        metalness: 0,
        emissive: style.glow,
        emissiveIntensity: 0,
      });
      const mesh = new THREE.Mesh(sphereGeometry, material);
      mesh.scale.setScalar(style.radius);
      mesh.position.copy(spawn);
      group.add(mesh);

      let rim: THREE.Mesh<THREE.SphereGeometry, THREE.MeshBasicMaterial> | null = null;
      if (style.rim) {
        rim = new THREE.Mesh(sphereGeometry, rimMaterial);
        rim.scale.setScalar(style.radius * RIM_SCALE);
        rim.position.copy(spawn);
        group.add(rim);
      }

      const label = createLabel(node.label, activeId === node.id ? LABEL_SELECTED_COLOR : LABEL_COLOR);
      label.sprite.position.copy(spawn);
      group.add(label.sprite);

      const view: NodeView = {
        id: node.id,
        kind: node.kind,
        radius: style.radius,
        mesh,
        rim,
        label,
        current: spawn.clone(),
        target: spawn.clone(),
        scale: 1,
        targetScale: 1,
      };
      viewByMesh.set(mesh, view);
      return view;
    };

    const destroyView = (view: NodeView) => {
      group.remove(view.mesh);
      view.mesh.material.dispose();
      viewByMesh.delete(view.mesh);
      if (view.rim) group.remove(view.rim);
      group.remove(view.label.sprite);
      view.label.texture.dispose();
      view.label.material.dispose();
    };

    const applySelection = () => {
      for (const view of views.values()) {
        const selected = view.id === activeId;
        view.targetScale = selected ? SELECT_SCALE : 1;
        view.mesh.material.emissiveIntensity = selected ? 0.8 : 0;
        const color = selected ? LABEL_SELECTED_COLOR : LABEL_COLOR;
        if (view.label.color !== color) drawLabel(view.label, view.label.text, color);
        view.label.material.opacity = selected ? 1 : 0.92;
      }
    };

    const updateCamera = () => {
      const verticalHalf = THREE.MathUtils.degToRad(camera.fov) / 2;
      const horizontalHalf = Math.atan(Math.tan(verticalHalf) * Math.max(0.2, camera.aspect));
      const half = Math.max(0.12, Math.min(verticalHalf, horizontalHalf));
      const distance = (fitRadius / Math.sin(half)) * 1.12 * orbit.zoom;
      const sinPolar = Math.sin(orbit.polar);
      camera.position.set(
        orbit.target.x + distance * sinPolar * Math.sin(orbit.azimuth),
        orbit.target.y + distance * Math.cos(orbit.polar),
        orbit.target.z + distance * sinPolar * Math.cos(orbit.azimuth),
      );
      camera.lookAt(orbit.target);
    };

    const rebuildEdges = (next: DecisionGraph) => {
      const pairs: Array<[NodeView, NodeView]> = [];
      for (const edge of next.edges) {
        const from = views.get(edge.from);
        const to = views.get(edge.to);
        if (from && to) pairs.push([from, to]);
      }
      edgePairs = pairs;
      const values = new Float32Array(pairs.length * 6);
      edgeValues = values;
      edgeGeometry?.dispose();
      edgeGeometry = new THREE.BufferGeometry();
      edgeAttribute = new THREE.BufferAttribute(values, 3);
      edgeAttribute.setUsage(THREE.DynamicDrawUsage);
      edgeGeometry.setAttribute("position", edgeAttribute);
      if (edgeMesh) group.remove(edgeMesh);
      edgeMesh = new THREE.LineSegments(edgeGeometry, edgeMaterial);
      edgeMesh.frustumCulled = false;
      group.add(edgeMesh);
    };

    const setGraph = (next: DecisionGraph) => {
      const layout = layoutGraph3D(next);
      const centroid = new THREE.Vector3();
      let placed = 0;
      for (const node of next.nodes) {
        const point = layout.positions[node.id];
        if (!point) continue;
        centroid.x += point.x;
        centroid.y += point.y;
        centroid.z += point.z;
        placed += 1;
      }
      if (placed > 0) centroid.multiplyScalar(1 / placed);

      let spread = 0;
      for (const node of next.nodes) {
        const point = layout.positions[node.id];
        if (!point) continue;
        const distance = Math.hypot(point.x - centroid.x, point.y - centroid.y, point.z - centroid.z);
        spread = Math.max(spread, distance);
      }
      fitRadius = Math.max(layout.radius, spread + 0.7);

      const present = new Set<string>();
      for (const node of next.nodes) {
        const point = layout.positions[node.id];
        if (!point) continue;
        present.add(node.id);
        const target = new THREE.Vector3(point.x - centroid.x, point.y - centroid.y, point.z - centroid.z);
        const existing = views.get(node.id);
        if (existing && existing.kind === node.kind) {
          existing.target.copy(target);
          if (existing.label.text !== node.label) {
            drawLabel(existing.label, node.label, existing.label.color);
          }
          continue;
        }
        const spawn = existing ? existing.current.clone() : target.clone().multiplyScalar(0.34);
        if (existing) {
          destroyView(existing);
          views.delete(node.id);
        }
        const created = createView(node, spawn);
        created.target.copy(target);
        views.set(node.id, created);
      }

      for (const [id, view] of Array.from(views.entries())) {
        if (present.has(id)) continue;
        destroyView(view);
        views.delete(id);
      }

      rebuildEdges(next);
      applySelection();
      updateCamera();
    };

    const setSelected = (nodeId: string | null) => {
      activeId = nodeId;
      applySelection();
    };

    const project = (nodeId: string): { x: number; y: number } | null => {
      const view = views.get(nodeId);
      if (!view) return null;
      group.updateMatrixWorld(true);
      camera.updateMatrixWorld(true);
      const point = view.current.clone().applyMatrix4(group.matrixWorld).project(camera);
      const rect = host.getBoundingClientRect();
      return {
        x: rect.left + ((point.x + 1) / 2) * rect.width,
        y: rect.top + ((1 - point.y) / 2) * rect.height,
      };
    };

    let pointerDown = false;
    let dragView: NodeView | null = null;
    let dragMoved = false;
    let downX = 0;
    let downY = 0;
    let lastX = 0;
    let lastY = 0;

    const raycaster = new THREE.Raycaster();
    const pointerNdc = new THREE.Vector2();
    const dragPlane = new THREE.Plane();
    const dragNormal = new THREE.Vector3();
    const dragOrigin = new THREE.Vector3();
    const dragOffset = new THREE.Vector3();
    const planePoint = new THREE.Vector3();
    const localPoint = new THREE.Vector3();

    const updatePointer = (event: PointerEvent) => {
      const rect = canvas.getBoundingClientRect();
      if (rect.width === 0 || rect.height === 0) return;
      pointerNdc.set(
        ((event.clientX - rect.left) / rect.width) * 2 - 1,
        -((event.clientY - rect.top) / rect.height) * 2 + 1,
      );
    };

    const pick = (): NodeView | null => {
      raycaster.setFromCamera(pointerNdc, camera);
      const objects = Array.from(views.values(), (view) => view.mesh);
      const hits = raycaster.intersectObjects(objects, false);
      if (hits.length === 0) return null;
      return viewByMesh.get(hits[0].object) ?? null;
    };

    const onPointerDown = (event: PointerEvent) => {
      if (event.button !== 0) return;
      event.preventDefault();
      updatePointer(event);
      pointerDown = true;
      dragMoved = false;
      downX = event.clientX;
      downY = event.clientY;
      lastX = event.clientX;
      lastY = event.clientY;
      canvas.setPointerCapture(event.pointerId);
      const hit = pick();
      dragView = hit;
      canvas.style.cursor = "grabbing";
      if (!hit) return;
      hit.mesh.getWorldPosition(dragOrigin);
      camera.getWorldDirection(dragNormal);
      dragPlane.setFromNormalAndCoplanarPoint(dragNormal, dragOrigin);
      raycaster.setFromCamera(pointerNdc, camera);
      const start = raycaster.ray.intersectPlane(dragPlane, planePoint);
      if (start) dragOffset.copy(dragOrigin).sub(start);
      else dragOffset.set(0, 0, 0);
    };

    const onPointerMove = (event: PointerEvent) => {
      updatePointer(event);
      const dx = event.clientX - lastX;
      const dy = event.clientY - lastY;
      lastX = event.clientX;
      lastY = event.clientY;

      if (!pointerDown) {
        canvas.style.cursor = pick() ? "pointer" : "grab";
        return;
      }

      if (dragView) {
        if (!dragMoved && Math.hypot(event.clientX - downX, event.clientY - downY) > DRAG_THRESHOLD_PX) {
          dragMoved = true;
        }
        if (dragMoved) {
          raycaster.setFromCamera(pointerNdc, camera);
          const point = raycaster.ray.intersectPlane(dragPlane, planePoint);
          if (point) {
            localPoint.copy(point).add(dragOffset);
            dragView.current.copy(group.worldToLocal(localPoint));
          }
        }
        return;
      }

      orbit.azimuth -= dx * 0.0052;
      orbit.polar = Math.min(MAX_POLAR, Math.max(MIN_POLAR, orbit.polar - dy * 0.0052));
    };

    const onPointerUp = (event: PointerEvent) => {
      if (!pointerDown) return;
      pointerDown = false;
      if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId);
      const moved = dragMoved || Math.hypot(event.clientX - downX, event.clientY - downY) > DRAG_THRESHOLD_PX;
      const hit = dragView;
      dragView = null;
      dragMoved = false;
      updatePointer(event);
      if (hit && !moved) onSelectRef.current(hit.id);
      canvas.style.cursor = pick() ? "pointer" : "grab";
    };

    const onPointerCancel = () => {
      pointerDown = false;
      dragView = null;
      dragMoved = false;
      canvas.style.cursor = "grab";
    };

    const onPointerLeave = () => {
      if (!pointerDown) canvas.style.cursor = "grab";
    };

    const onWheel = (event: WheelEvent) => {
      event.preventDefault();
      orbit.zoom = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, orbit.zoom * Math.exp(event.deltaY * 0.0012)));
    };

    const resize = () => {
      const width = Math.max(1, host.clientWidth);
      const height = Math.max(1, host.clientHeight);
      renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
      renderer.setSize(width, height, false);
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
      updateCamera();
    };

    const tick = (now: number) => {
      const delta = Math.min(0.05, Math.max(0, (now - last) / 1000));
      last = now;
      const spring = 1 - Math.exp(-delta * SPRING_RATE);
      const scaleSpring = 1 - Math.exp(-delta * SCALE_RATE);

      for (const view of views.values()) {
        if (view !== dragView) view.current.lerp(view.target, spring);
        view.scale += (view.targetScale - view.scale) * scaleSpring;
        const radius = view.radius * view.scale;
        view.mesh.position.copy(view.current);
        view.mesh.scale.setScalar(radius);
        if (view.rim) {
          view.rim.position.copy(view.current);
          view.rim.scale.setScalar(radius * RIM_SCALE);
        }
        view.label.sprite.position.set(
          view.current.x,
          view.current.y + radius + LABEL_HEIGHT * 0.55,
          view.current.z,
        );
      }

      if (edgeAttribute && edgeValues) {
        let offset = 0;
        for (const [from, to] of edgePairs) {
          edgeValues[offset] = from.current.x;
          edgeValues[offset + 1] = from.current.y;
          edgeValues[offset + 2] = from.current.z;
          edgeValues[offset + 3] = to.current.x;
          edgeValues[offset + 4] = to.current.y;
          edgeValues[offset + 5] = to.current.z;
          offset += 6;
        }
        edgeAttribute.needsUpdate = true;
      }

      if (!pointerDown) group.rotation.y += delta * IDLE_SPIN;

      updateCamera();
      renderer.render(scene, camera);
      raf = requestAnimationFrame(tick);
    };

    let last = performance.now();

    const dispose = () => {
      cancelAnimationFrame(raf);
      observer.disconnect();
      canvas.removeEventListener("pointerdown", onPointerDown);
      canvas.removeEventListener("pointermove", onPointerMove);
      canvas.removeEventListener("pointerup", onPointerUp);
      canvas.removeEventListener("pointercancel", onPointerCancel);
      canvas.removeEventListener("pointerleave", onPointerLeave);
      canvas.removeEventListener("wheel", onWheel);
      for (const view of views.values()) destroyView(view);
      views.clear();
      viewByMesh.clear();
      edgeGeometry?.dispose();
      edgeMaterial.dispose();
      sphereGeometry.dispose();
      rimMaterial.dispose();
      renderer.dispose();
      renderer.forceContextLoss();
      canvas.remove();
      if (debugWindow.__ink3d?.point === project) delete debugWindow.__ink3d;
      if (controlsRef.current === controls) controlsRef.current = null;
    };

    const controls: Controls = { setGraph, setSelected, project, dispose };
    const debugWindow = window as unknown as {
      __ink3d?: { point: (nodeId: string) => { x: number; y: number } | null };
    };
    debugWindow.__ink3d = { point: project };

    const observer = new ResizeObserver(() => resize());
    observer.observe(host);

    canvas.addEventListener("pointerdown", onPointerDown);
    canvas.addEventListener("pointermove", onPointerMove);
    canvas.addEventListener("pointerup", onPointerUp);
    canvas.addEventListener("pointercancel", onPointerCancel);
    canvas.addEventListener("pointerleave", onPointerLeave);
    canvas.addEventListener("wheel", onWheel, { passive: false });

    controlsRef.current = controls;
    resize();
    setGraph(graph);
    raf = requestAnimationFrame(tick);

    return dispose;
  }, []);

  useEffect(() => {
    controlsRef.current?.setGraph(graph);
  }, [graph]);

  return (
    <div
      ref={hostRef}
      style={{ position: "absolute", inset: 0, width: "100%", height: "100%", overflow: "hidden", touchAction: "none" }}
    />
  );
}
