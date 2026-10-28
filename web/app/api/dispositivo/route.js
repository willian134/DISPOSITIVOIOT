import { llamarEsp32 } from '@/lib/esp32';

export const dynamic = 'force-dynamic';

export async function GET() {
  const { status, datos } = await llamarEsp32('/api/estado');
  return Response.json({ online: status === 200, ...datos }, { status: 200 });
}